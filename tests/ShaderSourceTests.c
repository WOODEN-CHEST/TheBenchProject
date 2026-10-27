#include "TestSuites.h"
#include "AssetManager.h"
#include "EyeAdaptation.h"
#include "GameSkeleton.h"
#include "ShaderSource.h"
#include "SkeletalAnimation.h"
#include "WorldAtmosphere.h"
#include "WorldInstancing.h"
#include "WorldLod.h"
#include "WorldSkyLight.h"
#include "WorldVisibility.h"
#include "wr/WRFileSystem.h"
#include "wr/WRJSON.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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
        NULL, 0U, &self->Text, &self->SourceList);
}

/* Expands @p text (from TEST_MAIN_LOCATION) with @p defines into the fixture's Text and SourceList, which are cleared
 * first. */
static Error ShaderFixture_ExpandWithDefines(ShaderFixture* self, const char* text, const char* const* defines,
    size_t defineCount)
{
    GenericBuffer_Clear(&self->Text);
    GenericBuffer_Clear(&self->SourceList);
    AssetLocation Main = { AssetLocationType_File, (const unsigned char*)TEST_MAIN_LOCATION };
    return ShaderSource_ExpandIncludes(self->Manager, self->Types.Shader, &Main, (const unsigned char*)text,
        strlen(text), (const unsigned char* const*)defines, defineCount, &self->Text, &self->SourceList);
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
            (const unsigned char*)Text, sizeof(Text) - 1U, NULL, 0U, &Fixture.Text, &Fixture.SourceList),
            ErrorCode_InvalidAssetData);
        CHECK_TEXT(run, &Fixture.Text, "kept");
        TEST_CHECK(run, Fixture.SourceList._count == 0U);
    }
    ShaderFixture_Deconstruct(run, &Fixture);
}

/* A definition's defines follow the main file's #version line, which keeps its line numbers with a #line; without a
 * #version they come first; a #version line at the file's end gets its line break; only the first #version line gets
 * them. */
static void DefinesFollowTheVersionLine(TestRun* run)
{
    const char* const Defines[] = { "WORLD_INSTANCED", "_SECOND2" };
    ShaderFixture Fixture;
    if (ShaderFixture_ConstructInWorkDirectory(run, "defines", &Fixture)
        && ShaderFixture_Write(run, &Fixture, "common/a.glsl", "#ifdef WORLD_INSTANCED\nfloat a2;\n#endif\n"))
    {
        if (TEST_CHECK_SUCCESS(run, ShaderFixture_ExpandWithDefines(&Fixture,
            "// header\n#version 330\n#include \"common/a\"\nfloat main4;\n", Defines, 2U)))
        {
            CHECK_TEXT(run, &Fixture.Text, "// header\n#version 330\n#define WORLD_INSTANCED\n#define _SECOND2\n#line 3 0\n"
                "#line 1 1\n#ifdef WORLD_INSTANCED\nfloat a2;\n#endif\n#line 4 0\nfloat main4;\n");
            CHECK_TEXT(run, &Fixture.SourceList, "0 = \"main/fragment\", 1 = \"common/a\"");
        }
        if (TEST_CHECK_SUCCESS(run, ShaderFixture_ExpandWithDefines(&Fixture, "void main() {}\n", Defines, 1U)))
        {
            CHECK_TEXT(run, &Fixture.Text, "#define WORLD_INSTANCED\n#line 1 0\nvoid main() {}\n");
        }
        if (TEST_CHECK_SUCCESS(run, ShaderFixture_ExpandWithDefines(&Fixture, "#version 330", Defines, 1U)))
        {
            CHECK_TEXT(run, &Fixture.Text, "#version 330\n#define WORLD_INSTANCED\n#line 2 0\n");
        }
        if (TEST_CHECK_SUCCESS(run, ShaderFixture_ExpandWithDefines(&Fixture, "#version 330\r\n#version 330\n", Defines,
            1U)))
        {
            CHECK_TEXT(run, &Fixture.Text, "#version 330\r\n#define WORLD_INSTANCED\n#line 2 0\n#version 330\n");
        }
        // No defines: the text as before.
        if (TEST_CHECK_SUCCESS(run, ShaderFixture_ExpandWithDefines(&Fixture, "#version 330\nfloat x;\n", NULL, 0U)))
        {
            CHECK_TEXT(run, &Fixture.Text, "#version 330\nfloat x;\n");
        }
    }
    ShaderFixture_Deconstruct(run, &Fixture);
}

/* A define is a GLSL name a shader may define; the expander refuses others and leaves its output as it was. */
static void DefineNamesAreChecked(TestRun* run)
{
    const char* const Good[] = { "WORLD_INSTANCED", "a", "_x", "X9_y", "GLX", "GL", "G_L_", "Gl_X" };
    const char* const Bad[] = { "", "9A", "A-B", "A B", "GL_X", "GL_", "A__B", "__A", "A__", "\xC3\xA9t\xC3\xA9" };
    for (size_t Index = 0U; Index < (sizeof(Good) / sizeof(Good[0])); Index++)
    {
        if (!TEST_CHECK(run, ShaderSource_IsDefineName((const unsigned char*)Good[Index])))
        {
            printf("    refused \"%s\"\n", Good[Index]);
        }
    }
    for (size_t Index = 0U; Index < (sizeof(Bad) / sizeof(Bad[0])); Index++)
    {
        if (!TEST_CHECK(run, !ShaderSource_IsDefineName((const unsigned char*)Bad[Index])))
        {
            printf("    accepted \"%s\"\n", Bad[Index]);
        }
    }
    TEST_CHECK(run, !ShaderSource_IsDefineName(NULL));

    ShaderFixture Fixture;
    if (ShaderFixture_ConstructInWorkDirectory(run, "define_names", &Fixture))
    {
        const char* const Defines[] = { "GOOD", "GL_BAD" };
        GenericBuffer_AppendString(&Fixture.Text, (const unsigned char*)u8"kept");
        GenericBuffer_NullTerminate(&Fixture.Text);
        AssetLocation Main = { AssetLocationType_File, (const unsigned char*)TEST_MAIN_LOCATION };
        static const char Text[] = "#version 330\n";
        TEST_CHECK_ERROR(run, ShaderSource_ExpandIncludes(Fixture.Manager, Fixture.Types.Shader, &Main,
            (const unsigned char*)Text, sizeof(Text) - 1U, (const unsigned char* const*)Defines, 2U, &Fixture.Text,
            &Fixture.SourceList), ErrorCode_IllegalArgument);
        CHECK_TEXT(run, &Fixture.Text, "kept");
        TEST_CHECK_ERROR(run, ShaderSource_ExpandIncludes(Fixture.Manager, Fixture.Types.Shader, &Main,
            (const unsigned char*)Text, sizeof(Text) - 1U, NULL, 1U, &Fixture.Text, &Fixture.SourceList),
            ErrorCode_IllegalArgument);
    }
    ShaderFixture_Deconstruct(run, &Fixture);
}

/* Expands a shipped shader stage the way the shader loader does, with @p defines (@p defineCount of them). */
static bool ExpandShippedStageWithDefines(TestRun* run, ShaderFixture* self, const char* location,
    const char* const* defines, size_t defineCount)
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
        &Stage, Raw._data, Raw._count, (const unsigned char* const*)defines, defineCount, &self->Text,
        &self->SourceList));
    Memory_Free(Raw._data);
    return IsExpanded;
}

/* Expands a shipped shader stage the way the shader loader does for a definition without defines. */
static bool ExpandShippedStage(TestRun* run, ShaderFixture* self, const char* location)
{
    return ExpandShippedStageWithDefines(run, self, location, NULL, 0U);
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


/* The value of "const float <name> = <value>;" in an expanded text, or NaN without one. */
static double GetConstValue(const GenericBuffer* text, const char* name)
{
    char Opening[96];
    snprintf(Opening, sizeof(Opening), "const float %s = ", name);
    const char* Constant = (text->_count > 0U) ? strstr((const char*)text->_data, Opening) : NULL;
    return (Constant != NULL) ? strtod(Constant + strlen(Opening), NULL) : NAN;
}

/* The value of "const int <name> = <value>;" in an expanded text, or -1 without one. */
static long GetConstIntValue(const GenericBuffer* text, const char* name)
{
    char Opening[96];
    snprintf(Opening, sizeof(Opening), "const int %s = ", name);
    const char* Constant = (text->_count > 0U) ? strstr((const char*)text->_data, Opening) : NULL;
    return (Constant != NULL) ? strtol(Constant + strlen(Opening), NULL, 10) : -1L;
}

/* The atmosphere (T15): every shader that reads or builds its tables (and since T35 the haze's volume) takes them from
 * the one shared include, which declares the air, the view height and the tables once, and whose constants are its C
 * mirror's (source/WorldAtmosphere.h): the planet, the air's scale height, the ozone layer, the tables' sizes, what
 * counts as vertical, the volume's layout, steps and values and light pollution's reach, and (T36) the sun's light on
 * the one light scale, the lights' discs, the sky's light by height in the volume's last row and the irradiance table's
 * layout; the sky's old radiance is gone. */
static void ShippedAtmosphereMatchesItsMirror(TestRun* run)
{
    ShaderFixture Fixture;
    if (ShaderFixture_Construct(run, &Fixture)
        && TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, SHIPPED_ASSET_ROOT, &Fixture.Root))
        && TEST_CHECK_SUCCESS(run, AssetManager_AddSearchRoot(Fixture.Manager, Fixture.Root._data)))
    {
        const char* const Stages[] =
        {
            "atmosphere/transmittance", "atmosphere/multiscatter", "atmosphere/skyview", "atmosphere/volume",
            "atmosphere/irradiance", "sky/fragment", "stars/vertex", "pbr/fragment", "sprite/fragment",
            "terrain/fragment", "postfx/fragment"
        };
        for (size_t Index = 0U; Index < (sizeof(Stages) / sizeof(Stages[0])); Index++)
        {
            if (!ExpandShippedStage(run, &Fixture, Stages[Index]))
            {
                continue;
            }
            bool IsShared = BufferContains(&Fixture.SourceList, "\"common/atmosphere\"")
                && (CountOccurrences(&Fixture.Text, "uniform vec3 atmosphereRayleighScattering;") == 1U)
                && (CountOccurrences(&Fixture.Text, "uniform float atmosphereViewHeight;") == 1U)
                && (CountOccurrences(&Fixture.Text, "uniform bool atmosphereTablesActive;") == 1U)
                && (CountOccurrences(&Fixture.Text, "uniform sampler2D atmosphereTransmittanceTable;") == 1U)
                && (CountOccurrences(&Fixture.Text, "uniform sampler2D atmosphereSkyViewTable;") == 1U)
                && (CountOccurrences(&Fixture.Text, "uniform sampler2D atmosphereIrradianceTable;") == 1U)
                && (CountOccurrences(&Fixture.Text, "vec2 atmosphereSkyViewUnit(") == 1U)
                && (CountOccurrences(&Fixture.Text, "vec2 atmosphereTransmittanceUv(") == 1U);
            bool IsTheMirrors =
                (GetConstValue(&Fixture.Text, "ATMOSPHERE_R_PLANET") == WORLD_ATMOSPHERE_PLANET_RADIUS)
                && (GetConstValue(&Fixture.Text, "ATMOSPHERE_R_ATMOS") == WORLD_ATMOSPHERE_TOP_RADIUS)
                && (GetConstValue(&Fixture.Text, "ATMOSPHERE_RAYLEIGH_HEIGHT") == WORLD_ATMOSPHERE_RAYLEIGH_HEIGHT)
                && (GetConstValue(&Fixture.Text, "ATMOSPHERE_OZONE_CENTER") == WORLD_ATMOSPHERE_OZONE_CENTER)
                && (GetConstValue(&Fixture.Text, "ATMOSPHERE_OZONE_HALF_WIDTH") == WORLD_ATMOSPHERE_OZONE_HALF_WIDTH)
                && (GetConstValue(&Fixture.Text, "ATMOSPHERE_TRANSMITTANCE_WIDTH")
                    == (double)WORLD_ATMOSPHERE_TRANSMITTANCE_WIDTH)
                && (GetConstValue(&Fixture.Text, "ATMOSPHERE_TRANSMITTANCE_HEIGHT")
                    == (double)WORLD_ATMOSPHERE_TRANSMITTANCE_HEIGHT)
                && (GetConstValue(&Fixture.Text, "ATMOSPHERE_MULTISCATTER_SIZE")
                    == (double)WORLD_ATMOSPHERE_MULTISCATTER_SIZE)
                && (GetConstValue(&Fixture.Text, "ATMOSPHERE_SKY_VIEW_WIDTH") == (double)WORLD_ATMOSPHERE_SKY_VIEW_WIDTH)
                && (GetConstValue(&Fixture.Text, "ATMOSPHERE_SKY_VIEW_HEIGHT") == (double)WORLD_ATMOSPHERE_SKY_VIEW_HEIGHT)
                && (GetConstValue(&Fixture.Text, "ATMOSPHERE_SKY_VIEW_HALF_COUNT")
                    == (double)WORLD_ATMOSPHERE_SKY_VIEW_HALF_COUNT)
                && (GetConstValue(&Fixture.Text, "ATMOSPHERE_VERTICAL_EPSILON") == WORLD_ATMOSPHERE_VERTICAL_EPSILON);
            bool IsTheVolumes =
                (GetConstValue(&Fixture.Text, "ATMOSPHERE_VOLUME_SIZE") == (double)WORLD_ATMOSPHERE_VOLUME_SIZE)
                && (GetConstValue(&Fixture.Text, "ATMOSPHERE_VOLUME_SLICE_COUNT")
                    == (double)WORLD_ATMOSPHERE_VOLUME_SLICE_COUNT)
                && (CountOccurrences(&Fixture.Text, "const float ATMOSPHERE_VOLUME_LAST_DISTANCE_SLICE = "
                    "ATMOSPHERE_VOLUME_SLICE_COUNT - 2.0;") == 1U)
                && (WORLD_ATMOSPHERE_VOLUME_LAST_DISTANCE_SLICE == WORLD_ATMOSPHERE_VOLUME_SLICE_COUNT - 2)
                && (CountOccurrences(&Fixture.Text, "const float ATMOSPHERE_VOLUME_WIDTH = "
                    "ATMOSPHERE_VOLUME_SIZE*ATMOSPHERE_VOLUME_SLICE_COUNT;") == 1U)
                && (CountOccurrences(&Fixture.Text, "const float ATMOSPHERE_VOLUME_HEIGHT = "
                    "2.0*ATMOSPHERE_VOLUME_SIZE + 1.0;") == 1U)
                && (GetConstValue(&Fixture.Text, "ATMOSPHERE_VOLUME_NEAREST") == WORLD_ATMOSPHERE_VOLUME_NEAREST)
                && (GetConstValue(&Fixture.Text, "ATMOSPHERE_VOLUME_FARTHEST") == WORLD_ATMOSPHERE_VOLUME_FARTHEST)
                && (GetConstValue(&Fixture.Text, "ATMOSPHERE_VOLUME_NEAR_DISTANCE")
                    == WORLD_ATMOSPHERE_VOLUME_NEAR_DISTANCE)
                && (GetConstIntValue(&Fixture.Text, "ATMOSPHERE_VOLUME_NEAR_STEPS") == WORLD_ATMOSPHERE_VOLUME_NEAR_STEPS)
                && (GetConstIntValue(&Fixture.Text, "ATMOSPHERE_VOLUME_FAR_STEPS") == WORLD_ATMOSPHERE_VOLUME_FAR_STEPS)
                && (GetConstIntValue(&Fixture.Text, "ATMOSPHERE_VOLUME_RAY_STEPS") == WORLD_ATMOSPHERE_VOLUME_RAY_STEPS)
                && (GetConstIntValue(&Fixture.Text, "ATMOSPHERE_VOLUME_GROUND_HAZE_SUN")
                    == (long)WorldAtmosphereVolumeValue_GroundHazeSun)
                && (GetConstIntValue(&Fixture.Text, "ATMOSPHERE_VOLUME_GROUND_HAZE_MOON")
                    == (long)WorldAtmosphereVolumeValue_GroundHazeMoon)
                && (GetConstIntValue(&Fixture.Text, "ATMOSPHERE_VOLUME_GROUND_HAZE_AMBIENT")
                    == (long)WorldAtmosphereVolumeValue_GroundHazeAmbient)
                && (GetConstValue(&Fixture.Text, "ATMOSPHERE_LIGHT_POLLUTION_HEIGHT")
                    == WORLD_ATMOSPHERE_LIGHT_POLLUTION_HEIGHT)
                && (GetConstValue(&Fixture.Text, "ATMOSPHERE_LIGHT_POLLUTION_REACH")
                    == WORLD_ATMOSPHERE_LIGHT_POLLUTION_REACH)
                && (GetConstValue(&Fixture.Text, "ATMOSPHERE_SKY_DISTANCE") == WORLD_ATMOSPHERE_SKY_DISTANCE);
            bool IsTheLights =
                (GetConstValue(&Fixture.Text, "ATMOSPHERE_SUN_LIGHT") == WORLD_ATMOSPHERE_SUN_LIGHT)
                && (GetConstValue(&Fixture.Text, "ATMOSPHERE_LIGHT_DIAMETER") == WORLD_ATMOSPHERE_LIGHT_DIAMETER)
                && (GetConstIntValue(&Fixture.Text, "ATMOSPHERE_SKY_LIGHT_HEIGHT_COUNT")
                    == WORLD_ATMOSPHERE_SKY_LIGHT_HEIGHT_COUNT)
                && (GetConstValue(&Fixture.Text, "ATMOSPHERE_SKY_LIGHT_HIGHEST") == WORLD_ATMOSPHERE_SKY_LIGHT_HIGHEST)
                && (GetConstIntValue(&Fixture.Text, "ATMOSPHERE_SKY_LIGHT_FIRST_COLUMN")
                    == WORLD_ATMOSPHERE_SKY_LIGHT_FIRST_COLUMN)
                && (GetConstIntValue(&Fixture.Text, "ATMOSPHERE_SKY_LIGHT_SUN") == (long)WorldAtmosphereSkyLight_Sun)
                && (GetConstIntValue(&Fixture.Text, "ATMOSPHERE_SKY_LIGHT_MOON") == (long)WorldAtmosphereSkyLight_Moon)
                && (GetConstIntValue(&Fixture.Text, "ATMOSPHERE_SKY_LIGHT_SKY") == (long)WorldAtmosphereSkyLight_Sky)
                && (GetConstIntValue(&Fixture.Text, "ATMOSPHERE_SKY_LIGHT_GROUND") == (long)WorldAtmosphereSkyLight_Ground)
                && (GetConstIntValue(&Fixture.Text, "ATMOSPHERE_SKY_LIGHT_COUNT") == (long)WorldAtmosphereSkyLight_Count)
                && (GetConstValue(&Fixture.Text, "ATMOSPHERE_IRRADIANCE_WIDTH") == (double)WORLD_ATMOSPHERE_IRRADIANCE_WIDTH)
                && (GetConstValue(&Fixture.Text, "ATMOSPHERE_IRRADIANCE_HEIGHT")
                    == (double)WORLD_ATMOSPHERE_IRRADIANCE_HEIGHT)
                && (CountOccurrences(&Fixture.Text, "const float ATMOSPHERE_IRRADIANCE_TEXTURE_WIDTH = "
                    "ATMOSPHERE_IRRADIANCE_WIDTH + 1.0;") == 1U)
                && (GetConstValue(&Fixture.Text, "ATMOSPHERE_IRRADIANCE_LOWEST_ELEVATION")
                    == WORLD_ATMOSPHERE_IRRADIANCE_LOWEST_ELEVATION)
                && (GetConstValue(&Fixture.Text, "ATMOSPHERE_IRRADIANCE_LOG_FLOOR") == WORLD_ATMOSPHERE_IRRADIANCE_LOG_FLOOR)
                && (GetConstValue(&Fixture.Text, "ATMOSPHERE_IRRADIANCE_ELEVATION_SCALE")
                    == WORLD_ATMOSPHERE_IRRADIANCE_ELEVATION_SCALE)
                && (GetConstValue(&Fixture.Text, "ATMOSPHERE_IRRADIANCE_HEIGHT_SCALE")
                    == WORLD_ATMOSPHERE_IRRADIANCE_HEIGHT_SCALE)
                && !BufferContains(&Fixture.Text, "ATMOSPHERE_SUN_RADIANCE");
            if (!TEST_CHECK(run, IsShared && IsTheMirrors && IsTheVolumes && IsTheLights))
            {
                printf("    in \"%s\" (shared %d, the mirror's %d, the volume's %d, the lights' %d)\n", Stages[Index],
                    (int)IsShared, (int)IsTheMirrors, (int)IsTheVolumes, (int)IsTheLights);
            }
        }
        // The table builders' definitions load (the renderer asks for them by these names).
        size_t FailedCount = 1U;
        TEST_CHECK_SUCCESS(run, AssetManager_ReadDefinitions(Fixture.Manager, &FailedCount));
        TEST_CHECK(run, FailedCount == 0U);
        const unsigned char* const Names[] = { (const unsigned char*)u8"world_atmosphere_transmittance",
            (const unsigned char*)u8"world_atmosphere_multiscatter", (const unsigned char*)u8"world_atmosphere_skyview",
            (const unsigned char*)u8"world_atmosphere_volume", (const unsigned char*)u8"world_atmosphere_irradiance" };
        for (size_t Index = 0U; Index < (sizeof(Names) / sizeof(Names[0])); Index++)
        {
            bool Exists = false;
            TEST_CHECK(run, TEST_CHECK_SUCCESS(run, AssetManager_HasDefinition(Fixture.Manager, Fixture.Types.Shader,
                Names[Index], &Exists)) && Exists);
        }
    }
    ShaderFixture_Deconstruct(run, &Fixture);
}


/* Whether the numbers of "<opening><n numbers>)" in an expanded text are, in order, @p expected (within 1e-15). */
static bool ArrayIs(const GenericBuffer* text, const char* opening, const double* expected, int count)
{
    const char* Cursor = (text->_count > 0U) ? strstr((const char*)text->_data, opening) : NULL;
    if (Cursor == NULL)
    {
        return false;
    }
    Cursor += strlen(opening);
    for (int Index = 0; Index < count; Index++)
    {
        char* End = NULL;
        double Value = strtod(Cursor, &End);
        if ((End == Cursor) || (fabs(Value - expected[Index]) > 1e-15))
        {
            return false;
        }
        Cursor = End;
        while ((*Cursor == ',') || (*Cursor == ' ') || (*Cursor == '\n') || (*Cursor == '\r'))
        {
            Cursor++;
        }
    }
    return *Cursor == ')';
}

/* The sky's light on the surfaces (T36): the irradiance table's builder takes the C mirror's quadrature
 * (source/WorldSkyLight.h: the same rings, azimuths, stretches and depth, the same limits, the Gauss-Legendre nodes and
 * weights of its rings), and every lit shader takes the sky's light from the lighting include once (by height from the
 * volume's last row, or the camera's from its uniforms) with the ambient from its one function, as the volume's builder
 * writes that row (every block of it); the builders clamp the light above the air where the C mirror does, and the sun's
 * disc's radiance is its C twin's. */
static void ShippedSkyLightMatchesItsMirror(TestRun* run)
{
    ShaderFixture Fixture;
    if (ShaderFixture_Construct(run, &Fixture)
        && TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, SHIPPED_ASSET_ROOT, &Fixture.Root))
        && TEST_CHECK_SUCCESS(run, AssetManager_AddSearchRoot(Fixture.Manager, Fixture.Root._data)))
    {
        if (ExpandShippedStage(run, &Fixture, "atmosphere/irradiance"))
        {
            // Gauss-Legendre's 6 nodes on [-1, 1] (the roots of the Legendre polynomial P6) and their weights.
            const double Nodes[6] = { -0.9324695142031521, -0.6612093864662645, -0.2386191860831969,
                0.2386191860831969, 0.6612093864662645, 0.9324695142031521 };
            const double Weights[6] = { 0.1713244923791704, 0.3607615730481386, 0.4679139345726910,
                0.4679139345726910, 0.3607615730481386, 0.1713244923791704 };
            bool IsTheMirrors =
                (GetConstIntValue(&Fixture.Text, "IRRADIANCE_ZENITH_NODES") == WORLD_SKY_LIGHT_ZENITH_NODES)
                && (GetConstIntValue(&Fixture.Text, "IRRADIANCE_AZIMUTHS") == WORLD_SKY_LIGHT_AZIMUTHS)
                && (GetConstIntValue(&Fixture.Text, "IRRADIANCE_MIE_RINGS") == WORLD_SKY_LIGHT_MIE_RINGS)
                && (GetConstIntValue(&Fixture.Text, "IRRADIANCE_MIE_AZIMUTHS") == WORLD_SKY_LIGHT_MIE_AZIMUTHS)
                && (GetConstIntValue(&Fixture.Text, "IRRADIANCE_STEPS") == WORLD_SKY_LIGHT_STEPS)
                && (GetConstIntValue(&Fixture.Text, "IRRADIANCE_MIE_STEPS") == WORLD_SKY_LIGHT_MIE_STEPS)
                && (GetConstValue(&Fixture.Text, "IRRADIANCE_MIE_DEPTH") == WORLD_SKY_LIGHT_MIE_DEPTH)
                && (GetConstValue(&Fixture.Text, "IRRADIANCE_LIGHT_POLLUTION_DEPTH")
                    == WORLD_SKY_LIGHT_LIGHT_POLLUTION_DEPTH)
                && (GetConstValue(&Fixture.Text, "IRRADIANCE_SERIES_LIMIT") == 1e-3)
                && (GetConstValue(&Fixture.Text, "IRRADIANCE_ISOTROPIC_LIMIT") == 1e-4)
                && (GetConstValue(&Fixture.Text, "IRRADIANCE_MAX_GLOW") == WORLD_ENVIRONMENT_MAX_SUN_GLOW)
                // The arrays below are the 6-node rule: a change of the rings needs new ones here.
                && (WORLD_SKY_LIGHT_ZENITH_NODES == 6)
                && ArrayIs(&Fixture.Text, "GAUSS_NODES[IRRADIANCE_ZENITH_NODES] = float[](", Nodes, 6)
                && ArrayIs(&Fixture.Text, "GAUSS_WEIGHTS[IRRADIANCE_ZENITH_NODES] = float[](", Weights, 6);
            TEST_CHECK(run, IsTheMirrors);
        }
        // The volume's builder writes every block of the last row, and clamps the light above the air where the C
        // mirror does (WORLD_SKY_LIGHT_MAX_LIGHT); so does the sky view's.
        if (ExpandShippedStage(run, &Fixture, "atmosphere/volume"))
        {
            TEST_CHECK(run, (CountOccurrences(&Fixture.Text, "vec3 SkyLightValue(") == 1U)
                && (CountOccurrences(&Fixture.Text, "int block = texel.x - ATMOSPHERE_SKY_LIGHT_FIRST_COLUMN;") == 1U)
                && (CountOccurrences(&Fixture.Text,
                    "block < ATMOSPHERE_SKY_LIGHT_COUNT*ATMOSPHERE_SKY_LIGHT_HEIGHT_COUNT") == 1U)
                && (GetConstValue(&Fixture.Text, "HALF_FLOAT_MAX") == WORLD_SKY_LIGHT_MAX_LIGHT));
        }
        if (ExpandShippedStage(run, &Fixture, "atmosphere/skyview"))
        {
            TEST_CHECK(run, GetConstValue(&Fixture.Text, "HALF_FLOAT_MAX") == WORLD_SKY_LIGHT_MAX_LIGHT);
        }
        // The sun's disc's radiance is its C twin's: the renderer clamps the disc's intensity and draws the light before
        // T36's disc by it.
        if (ExpandShippedStage(run, &Fixture, "sky/fragment"))
        {
            TEST_CHECK(run, GetConstValue(&Fixture.Text, "SUN_DISC_RADIANCE") == WORLD_ATMOSPHERE_SUN_DISC_RADIANCE);
        }
        const char* const Lit[] = { "pbr/fragment", "terrain/fragment", "sprite/fragment" };
        const char* const Gone[] = { "ATMOSPHERE_SUN_RADIANCE", "uniform vec3 ambientColor", "uniform vec3 sunColor",
            "uniform vec3 moonColor", "moonAmbient" };
        for (size_t Index = 0U; Index < (sizeof(Lit) / sizeof(Lit[0])); Index++)
        {
            if (!ExpandShippedStage(run, &Fixture, Lit[Index]))
            {
                continue;
            }
            bool IsShared = BufferContains(&Fixture.SourceList, "\"common/lighting\"")
                && (CountOccurrences(&Fixture.Text, "SkyLightValues SkyLightAt(vec3 worldPos)") == 1U)
                && (CountOccurrences(&Fixture.Text, "vec3 AmbientLight(SkyLightValues light, vec3 n)") == 1U)
                && (CountOccurrences(&Fixture.Text, "uniform bool skyLightPerHeight;") == 1U)
                && (CountOccurrences(&Fixture.Text, "SkyLightValues light = SkyLightAt(fragPosition);") == 1U)
                && (CountOccurrences(&Fixture.Text, "AmbientLight(light, n)") == 1U);
            bool IsOldLightGone = true;
            for (size_t Name = 0U; Name < (sizeof(Gone) / sizeof(Gone[0])); Name++)
            {
                IsOldLightGone = IsOldLightGone && !BufferContains(&Fixture.Text, Gone[Name]);
            }
            if (!TEST_CHECK(run, IsShared && IsOldLightGone))
            {
                printf("    in \"%s\" (shared %d, the old light gone %d)\n", Lit[Index], (int)IsShared,
                    (int)IsOldLightGone);
            }
        }
    }
    ShaderFixture_Deconstruct(run, &Fixture);
}


/* The haze (T35): every lit shader applies it once through the lighting include (an additive sprite takes its
 * transmittance alone), the sky pass (the discs only where a ray misses the planet), the stars and the outlines take it
 * from the same include, the fog is gone from all of them, the include's series limits and its scaled erfc's coefficients
 * are its C mirror's (WorldAtmosphere.c), and the G-buffer's shaders encode an outlined surface's haze from the value the
 * outline pass decodes it with, above its outline threshold. */
static void ShippedShadersShareOneHaze(TestRun* run)
{
    ShaderFixture Fixture;
    if (ShaderFixture_Construct(run, &Fixture)
        && TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, SHIPPED_ASSET_ROOT, &Fixture.Root))
        && TEST_CHECK_SUCCESS(run, AssetManager_AddSearchRoot(Fixture.Manager, Fixture.Root._data)))
    {
        typedef struct HazeStageStruct
        {
            const char* Location;
            const char* Use;
            size_t UseCount;
        } HazeStage;
        const HazeStage Stages[] =
        {
            { "pbr/fragment", "color = ApplyAerialPerspective(color, fragPosition);", 1U },
            { "terrain/fragment", "color = ApplyAerialPerspective(color, fragPosition);", 1U },
            { "sprite/fragment", "color = ApplyAerialPerspective(color, fragPosition);", 1U },
            { "sprite/fragment", "color *= AerialTransmittance(fragPosition);", 1U },
            { "sky/fragment", "mistTransmittance = hazeGroundInFrontOfSky(uv, rayDir, toEnd,", 1U },
            { "sky/fragment", "bool isBeyondAirShown = atmosphereTablesActive ? (toGround < 0.0) : (rayDir.y > -0.02);", 1U },
            { "stars/vertex", "*hazeGroundTransmittance(direction, ATMOSPHERE_SKY_DISTANCE);", 1U },
            { "postfx/fragment", "haze = hazeTransmittance(uv, direction, toPoint, multiplier);", 1U },
        };
        const char* const Gone[] = { "ApplyFog", "FogFactor", "fogSkyColor", "fogDensity", "skySunIntensity" };
        for (size_t Index = 0U; Index < (sizeof(Stages) / sizeof(Stages[0])); Index++)
        {
            if (!ExpandShippedStage(run, &Fixture, Stages[Index].Location))
            {
                continue;
            }
            bool IsShared = BufferContains(&Fixture.SourceList, "\"common/haze\"")
                && (CountOccurrences(&Fixture.Text, "uniform sampler2D atmosphereVolume;") == 1U)
                && (CountOccurrences(&Fixture.Text, "float hazeGroundDepth(") == 1U)
                && (CountOccurrences(&Fixture.Text, "float hazeGroundDepthCurved(") == 1U)
                // WorldAtmosphere_GetScaledErfc's coefficients, which the tests check against the C library's erfc.
                && (CountOccurrences(&Fixture.Text, "return t*exp(-1.26551223 + t*(1.00002368 + t*(0.37409196 + "
                    "t*(0.09678418 + t*(-0.18628806 + t*(0.27886807") == 1U)
                && (CountOccurrences(&Fixture.Text, "+ t*(-1.13520398 + t*(1.48851587 + t*(-0.82215223 + "
                    "t*0.17087277)))))))));") == 1U)
                && (GetConstValue(&Fixture.Text, "HAZE_SERIES_LIMIT") == 1e-3)
                && (GetConstValue(&Fixture.Text, "HAZE_SLOPE_LIMIT") == 1e-3)
                && (GetConstValue(&Fixture.Text, "HAZE_LIGHT_MAX") == 65504.0);
            bool IsUsed = CountOccurrences(&Fixture.Text, Stages[Index].Use) == Stages[Index].UseCount;
            bool IsFogGone = true;
            for (size_t Name = 0U; Name < (sizeof(Gone) / sizeof(Gone[0])); Name++)
            {
                IsFogGone = IsFogGone && !BufferContains(&Fixture.Text, Gone[Name]);
            }
            if (!TEST_CHECK(run, IsShared && IsUsed && IsFogGone))
            {
                printf("    in \"%s\" (shared %d, used %d, no fog %d)\n", Stages[Index].Location, (int)IsShared,
                    (int)IsUsed, (int)IsFogGone);
            }
        }
        // The lit shaders take a draw's multiplier once, from the lighting include.
        const char* const Lit[] = { "pbr/fragment", "terrain/fragment", "sprite/fragment" };
        for (size_t Index = 0U; Index < (sizeof(Lit) / sizeof(Lit[0])); Index++)
        {
            if (ExpandShippedStage(run, &Fixture, Lit[Index]))
            {
                TEST_CHECK(run, CountOccurrences(&Fixture.Text, "uniform float hazeMultiplier;") == 1U);
            }
        }
        // The outlined surfaces' alpha: written from OUTLINE_HAZE_BASE, read back with the same, above OUTLINE_ON.
        const char* const Encoders[] = { "normal/fragment", "normal_sprite/fragment" };
        double Base = NAN;
        if (ExpandShippedStage(run, &Fixture, "postfx/fragment"))
        {
            Base = GetConstValue(&Fixture.Text, "OUTLINE_HAZE_BASE");
            TEST_CHECK(run, (Base > GetConstValue(&Fixture.Text, "OUTLINE_ON")) && (Base < 1.0));
        }
        for (size_t Index = 0U; Index < (sizeof(Encoders) / sizeof(Encoders[0])); Index++)
        {
            if (ExpandShippedStage(run, &Fixture, Encoders[Index]))
            {
                TEST_CHECK(run, (GetConstValue(&Fixture.Text, "OUTLINE_HAZE_BASE") == Base)
                    && (CountOccurrences(&Fixture.Text, "uniform float hazeMultiplier;") == 1U)
                    && (CountOccurrences(&Fixture.Text,
                        "mix(OUTLINE_HAZE_BASE, 1.0, clamp(hazeMultiplier, 0.0, 1.0))") == 1U));
            }
        }
    }
    ShaderFixture_Deconstruct(run, &Fixture);
}

/* Whether an expanded text's DITHER_BAYER holds the 16 values source/WorldVisibility.c mirrors in C, in its order (row
 * by row from the target's bottom row). */
static bool BayerMatchesTheMirror(const GenericBuffer* text)
{
    static const char Opening[] = "DITHER_BAYER[16] = float[16](";
    const char* Cursor = (text->_count > 0U) ? strstr((const char*)text->_data, Opening) : NULL;
    if (Cursor == NULL)
    {
        return false;
    }
    Cursor += strlen(Opening);
    for (uint32_t Index = 0U; Index < WORLD_VISIBILITY_DITHER_LEVELS; Index++)
    {
        char* End = NULL;
        double Value = strtod(Cursor, &End);
        float Expected = WorldVisibility_GetDitherThreshold(Index % WORLD_VISIBILITY_DITHER_SIZE,
            Index / WORLD_VISIBILITY_DITHER_SIZE) * (float)WORLD_VISIBILITY_DITHER_LEVELS - 0.5f;
        if ((End == Cursor) || (fabs(Value - (double)Expected) > 1e-6))
        {
            return false;
        }
        Cursor = End;
        while ((*Cursor == ',') || (*Cursor == ' ') || (*Cursor == '\n') || (*Cursor == '\r'))
        {
            Cursor++;
        }
    }
    return *Cursor == ')';
}

/* Every geometry shader of the world (models and sprites; the scene, the G-buffer, the cascades and the cube shadow)
 * dithers through the one shared include (the draw's fade and its part of a LOD cross-fade) and tests it once, and the
 * include's Bayer matrix and comparisons are the ones the C mirror (source/WorldVisibility.c) and the tests use. */
static void ShippedGeometryShadersShareOneDither(TestRun* run)
{
    ShaderFixture Fixture;
    if (ShaderFixture_Construct(run, &Fixture)
        && TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, SHIPPED_ASSET_ROOT, &Fixture.Root))
        && TEST_CHECK_SUCCESS(run, AssetManager_AddSearchRoot(Fixture.Manager, Fixture.Root._data)))
    {
        const char* const Stages[] =
        {
            "pbr/fragment", "normal/fragment", "depth/fragment", "cube_depth/fragment", "sprite/fragment",
            "normal_sprite/fragment", "sprite_depth/fragment", "sprite_cube_depth/fragment", "terrain/fragment"
        };
        for (size_t Index = 0U; Index < (sizeof(Stages) / sizeof(Stages[0])); Index++)
        {
            if (ExpandShippedStage(run, &Fixture, Stages[Index]))
            {
                bool HasUniforms = (CountOccurrences(&Fixture.Text, "uniform float ditherFade;") == 1U)
                    && (CountOccurrences(&Fixture.Text, "uniform float ditherLodProgress;") == 1U)
                    && (CountOccurrences(&Fixture.Text, "uniform bool ditherIsIncomingLod;") == 1U)
                    && (CountOccurrences(&Fixture.Text, "uniform vec2 ditherPixelScale;") == 1U);
                // The include defines it once, the shader tests it once.
                bool IsTested = (CountOccurrences(&Fixture.Text, "DitherDiscard()") == 2U)
                    && (CountOccurrences(&Fixture.Text, "bool DitherDiscard()") == 1U);
                // Its comparisons are the C mirror's (WorldVisibility_IsDitherDiscarded), which the LOD tests prove
                // complementary: a changed formula fails here until the mirror and this check change with it. Without
                // WORLD_INSTANCED the uniforms feed it.
                bool IsTheMirrorsFormula =
                    (CountOccurrences(&Fixture.Text, "bool isKeptByFade = threshold > fade;") == 1U)
                    && (CountOccurrences(&Fixture.Text, "bool isKeptByLod = isIncomingLod ? "
                        "(threshold <= lodProgress) : (threshold > lodProgress);") == 1U)
                    && (CountOccurrences(&Fixture.Text, "return !(isKeptByFade && isKeptByLod);") == 1U)
                    && (CountOccurrences(&Fixture.Text,
                        "return DitherDiscardValues(ditherFade, ditherLodProgress, ditherIsIncomingLod);") == 1U);
                if (!TEST_CHECK(run, HasUniforms && IsTested && IsTheMirrorsFormula
                    && BufferContains(&Fixture.SourceList, "\"common/dither\"") && BayerMatchesTheMirror(&Fixture.Text)))
                {
                    printf("    in \"%s\"\n", Stages[Index]);
                }
            }
        }
    }
    ShaderFixture_Deconstruct(run, &Fixture);
}


/* Every skinned vertex stage (T29) skins through the one shared include, once: the bone attributes raylib binds, the
 * palette of three rows per bone the C side fills (SKELETAL_PALETTE_FLOATS_PER_BONE), no larger than the largest
 * skeleton the renderer probes for (GAME_SKELETON_MAX_BONES). */
static void ShippedSkinnedStagesShareOneSkinning(TestRun* run)
{
    ShaderFixture Fixture;
    if (ShaderFixture_Construct(run, &Fixture)
        && TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, SHIPPED_ASSET_ROOT, &Fixture.Root))
        && TEST_CHECK_SUCCESS(run, AssetManager_AddSearchRoot(Fixture.Manager, Fixture.Root._data)))
    {
        TEST_CHECK(run, SKELETAL_PALETTE_FLOATS_PER_BONE == 12U);
        const char* const Stages[] = { "pbr/skinned", "normal/skinned", "depth/skinned", "cube_depth/skinned",
            "unlit/skinned" };
        for (size_t Index = 0U; Index < (sizeof(Stages) / sizeof(Stages[0])); Index++)
        {
            if (!ExpandShippedStage(run, &Fixture, Stages[Index]))
            {
                continue;
            }
            const char* Define = strstr((const char*)Fixture.Text._data, "#define SKINNING_MAX_BONES ");
            long MaxBones = (Define != NULL) ? strtol(Define + strlen("#define SKINNING_MAX_BONES "), NULL, 10) : 0L;
            bool IsShared = BufferContains(&Fixture.SourceList, "\"common/skinning\"")
                && (CountOccurrences(&Fixture.Text, "uniform vec4 bonePalette[SKINNING_MAX_BONES*3];") == 1U)
                && (CountOccurrences(&Fixture.Text, "in vec4 vertexBoneIndices;") == 1U)
                && (CountOccurrences(&Fixture.Text, "in vec4 vertexBoneWeights;") == 1U)
                && (CountOccurrences(&Fixture.Text, "Skinning_GetRows(row0, row1, row2);") == 1U)
                && (MaxBones > 0L) && (MaxBones <= (long)GAME_SKELETON_MAX_BONES);
            if (!TEST_CHECK(run, IsShared))
            {
                printf("    in \"%s\"\n", Stages[Index]);
            }
        }
    }
    ShaderFixture_Deconstruct(run, &Fixture);
}


/* Reads "<shipped>/shaders/<relativePath>" into @p outText (NUL-terminated). */
static bool ReadShippedText(TestRun* run, const char* relativePath, GenericBuffer* outText)
{
    char Relative[128];
    snprintf(Relative, sizeof(Relative), "%s/shaders/%s", SHIPPED_ASSET_ROOT, relativePath);
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, 0U, sizeof(unsigned char));
    bool IsRead = TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, Relative, &Path))
        && TEST_CHECK_SUCCESS(run, FileSystem_ReadAllBytes(Path._data, outText))
        && GenericBuffer_NullTerminate(outText);
    Memory_Free(Path._data);
    return IsRead;
}

/* The value of "#define <name> <value>" in an expanded text, or NaN without one. */
static double GetDefinedValue(const GenericBuffer* text, const char* name)
{
    char Opening[96];
    snprintf(Opening, sizeof(Opening), "#define %s ", name);
    const char* Define = (text->_count > 0U) ? strstr((const char*)text->_data, Opening) : NULL;
    return (Define != NULL) ? strtod(Define + strlen(Opening), NULL) : NAN;
}

/* The instanced twins (B03): each definition loads, defines WORLD_INSTANCED and pairs its own vertex stage with its rigid
 * shader's fragment stage. Every instanced vertex stage places its copies through the one shared include, which takes
 * the records at locations 9 to 13 and whose constants are its C mirror's (source/WorldInstancing.h); with the define,
 * the fragment stages' dither reads the copy's flat values through the formula the uniforms use. */
static void ShippedInstancedTwinsShareTheirStages(TestRun* run)
{
    static const char* const Definitions[][3] =
    {
        { "pbr/pbr_instanced.json", "pbr/instanced", "pbr/fragment" },
        { "normal/normal_instanced.json", "normal/instanced", "normal/fragment" },
        { "depth/depth_instanced.json", "depth/instanced", "depth/fragment" },
        { "cube_depth/cube_depth_instanced.json", "cube_depth/instanced", "cube_depth/fragment" }
    };
    const char* const Instanced[] = { "WORLD_INSTANCED" };
    ShaderFixture Fixture;
    size_t FailedCount = 1U;
    GenericBuffer Definition;
    GenericBuffer_AllocateVariable(&Definition, 0U, sizeof(unsigned char));
    if (ShaderFixture_Construct(run, &Fixture)
        && TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, SHIPPED_ASSET_ROOT, &Fixture.Root))
        && TEST_CHECK_SUCCESS(run, AssetManager_AddSearchRoot(Fixture.Manager, Fixture.Root._data))
        && TEST_CHECK_SUCCESS(run, AssetManager_ReadDefinitions(Fixture.Manager, &FailedCount)))
    {
        TEST_CHECK(run, FailedCount == 0U);
        const unsigned char* const Names[] = { (const unsigned char*)u8"world_pbr_instanced",
            (const unsigned char*)u8"world_normal_instanced", (const unsigned char*)u8"world_depth_instanced",
            (const unsigned char*)u8"world_cube_depth_instanced" };
        for (size_t Index = 0U; Index < (sizeof(Names) / sizeof(Names[0])); Index++)
        {
            bool Exists = false;
            TEST_CHECK(run, TEST_CHECK_SUCCESS(run, AssetManager_HasDefinition(Fixture.Manager, Fixture.Types.Shader,
                Names[Index], &Exists)) && Exists);
        }
        for (size_t Index = 0U; Index < (sizeof(Definitions) / sizeof(Definitions[0])); Index++)
        {
            GenericBuffer_Clear(&Definition);
            bool IsPaired = ReadShippedText(run, Definitions[Index][0], &Definition)
                && BufferContains(&Definition, "\"defines\": [\"WORLD_INSTANCED\"]");
            char Location[64];
            snprintf(Location, sizeof(Location), "\"vertex_location\": \"%s\"", Definitions[Index][1]);
            IsPaired = IsPaired && BufferContains(&Definition, Location);
            snprintf(Location, sizeof(Location), "\"fragment_location\": \"%s\"", Definitions[Index][2]);
            IsPaired = IsPaired && BufferContains(&Definition, Location);

            bool IsPlaced = ExpandShippedStageWithDefines(run, &Fixture, Definitions[Index][1], Instanced, 1U)
                && (strncmp((const char*)Fixture.Text._data, "#version 330\n#define WORLD_INSTANCED\n#line 2 0\n", 46U)
                    == 0)
                && BufferContains(&Fixture.SourceList, "\"common/instancing\"")
                && (CountOccurrences(&Fixture.Text, "layout(location = 9) in vec4 instanceRow0;") == 1U)
                && (CountOccurrences(&Fixture.Text, "layout(location = 10) in vec4 instanceRow1;") == 1U)
                && (CountOccurrences(&Fixture.Text, "layout(location = 11) in vec4 instanceRow2;") == 1U)
                && (CountOccurrences(&Fixture.Text, "layout(location = 12) in vec4 instanceTint;") == 1U)
                && (CountOccurrences(&Fixture.Text, "layout(location = 13) in float instanceRandom;") == 1U)
                && (CountOccurrences(&Fixture.Text, "if (!Instancing_IsShown(matModel))") == 1U)
                && (CountOccurrences(&Fixture.Text, "Instancing_MoveVertex(vertexPosition)") == 1U)
                && (GetDefinedValue(&Fixture.Text, "INSTANCE_PASS_LIT") == (double)WorldInstancePass_Lit)
                && (GetDefinedValue(&Fixture.Text, "INSTANCE_PASS_GBUFFER") == (double)WorldInstancePass_GBuffer)
                && (GetDefinedValue(&Fixture.Text, "INSTANCE_PASS_SHADOW") == (double)WorldInstancePass_Shadow)
                && (GetDefinedValue(&Fixture.Text, "INSTANCE_MAX_BANDS") == (double)WORLD_INSTANCE_MAX_BANDS)
                && (GetDefinedValue(&Fixture.Text, "INSTANCE_SOLID_SHARE") == (double)WORLD_LOD_SOLID_SHARE)
                && (GetDefinedValue(&Fixture.Text, "INSTANCE_GBUFFER_FADE_LIMIT")
                    == (double)WORLD_VISIBILITY_GBUFFER_FADE_LIMIT);

            bool IsDithered = ExpandShippedStageWithDefines(run, &Fixture, Definitions[Index][2], Instanced, 1U)
                && (CountOccurrences(&Fixture.Text, "#define WORLD_INSTANCED\n") == 1U)
                && (CountOccurrences(&Fixture.Text, "flat in float fragDitherFade;") == 1U)
                && (CountOccurrences(&Fixture.Text, "flat in float fragDitherLodProgress;") == 1U)
                && (CountOccurrences(&Fixture.Text, "flat in float fragDitherIsIncomingLod;") == 1U)
                && (CountOccurrences(&Fixture.Text, "return DitherDiscardValues(fragDitherFade, fragDitherLodProgress, "
                    "fragDitherIsIncomingLod > 0.5);") == 1U)
                && BayerMatchesTheMirror(&Fixture.Text);
            if (!TEST_CHECK(run, IsPaired && IsPlaced && IsDithered))
            {
                printf("    in \"%s\" (paired %d, placed %d, dithered %d)\n", Definitions[Index][0], (int)IsPaired,
                    (int)IsPlaced, (int)IsDithered);
            }
        }
    }
    Memory_Free(Definition._data);
    ShaderFixture_Deconstruct(run, &Fixture);
}

/* The eye's meter (T37): its blocks and samples are its C mirror's (source/EyeAdaptation.h, whose readback and weights
 * take that many blocks), and its window is the one vec3 the renderer uploads. */
static void ShippedMeterMatchesItsMirror(TestRun* run)
{
    ShaderFixture Fixture;
    if (ShaderFixture_Construct(run, &Fixture)
        && TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, SHIPPED_ASSET_ROOT, &Fixture.Root))
        && TEST_CHECK_SUCCESS(run, AssetManager_AddSearchRoot(Fixture.Manager, Fixture.Root._data)))
    {
        if (ExpandShippedStage(run, &Fixture, "luminance/fragment"))
        {
            TEST_CHECK(run, (GetConstIntValue(&Fixture.Text, "METER_WIDTH") == EYE_ADAPTATION_METER_WIDTH)
                && (GetConstIntValue(&Fixture.Text, "METER_HEIGHT") == EYE_ADAPTATION_METER_HEIGHT)
                && (GetConstIntValue(&Fixture.Text, "SAMPLES") == EYE_ADAPTATION_METER_SAMPLES)
                && (CountOccurrences(&Fixture.Text, "uniform vec3 window;") == 1U));
        }
    }
    ShaderFixture_Deconstruct(run, &Fixture);
}

/* Every stage that reads the camera's depth or writes its own (T39: the eye's meter, the bloom's prefilter, the sun
 * shafts, the crisp composite, the AO, the outlines and the stars) takes the depth convention from the one shared
 * include, with the renderer's depthReversed uniform once, and keeps no sky depth of its own; the include's tests are the
 * conventions GLDepth.h states (reversed: the sky exactly 0, clip control's depth as stored, nearer greater; standard:
 * raylib's, the sky at the cleared 1). */
static void ShippedDepthReadersShareOneConvention(TestRun* run)
{
    ShaderFixture Fixture;
    if (ShaderFixture_Construct(run, &Fixture)
        && TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, SHIPPED_ASSET_ROOT, &Fixture.Root))
        && TEST_CHECK_SUCCESS(run, AssetManager_AddSearchRoot(Fixture.Manager, Fixture.Root._data)))
    {
        const char* const Stages[] =
        {
            "luminance/fragment", "bloom_prefilter/fragment", "sunshafts/fragment", "crisp_composite/fragment",
            "ao/fragment", "postfx/fragment", "stars/vertex"
        };
        for (size_t Index = 0U; Index < (sizeof(Stages) / sizeof(Stages[0])); Index++)
        {
            if (ExpandShippedStage(run, &Fixture, Stages[Index]))
            {
                bool IsShared = BufferContains(&Fixture.SourceList, "\"common/depth\"")
                    && (CountOccurrences(&Fixture.Text, "uniform float depthReversed;") == 1U);
                bool HasNoOwnDepth = (CountOccurrences(&Fixture.Text, "SKY_DEPTH") == 0U)
                    && (CountOccurrences(&Fixture.Text, "FAR_DEPTH") == 0U)
                    && (CountOccurrences(&Fixture.Text, "d*2.0 - 1.0") == 0U);
                bool IsTheConvention = (CountOccurrences(&Fixture.Text,
                        "return (depthReversed > 0.5) ? (depth <= 0.0) : (depth >= 0.9999);") == 1U)
                    && (CountOccurrences(&Fixture.Text, "return (depthReversed > 0.5) ? depth : depth*2.0 - 1.0;") == 1U)
                    && (CountOccurrences(&Fixture.Text,
                        "return (depthReversed > 0.5) ? (a < b*(1.0 - tolerance)) : (a > b + tolerance);") == 1U);
                if (!TEST_CHECK(run, IsShared && HasNoOwnDepth && IsTheConvention))
                {
                    printf("    in \"%s\"\n", Stages[Index]);
                }
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
        { "defines_follow_the_version_line", DefinesFollowTheVersionLine },
        { "define_names_are_checked", DefineNamesAreChecked },
        { "shipped_sky_and_pbr_share_one_atmosphere", ShippedSkyAndPbrShareOneAtmosphere },
        { "shipped_atmosphere_matches_its_mirror", ShippedAtmosphereMatchesItsMirror },
        { "shipped_sky_light_matches_its_mirror", ShippedSkyLightMatchesItsMirror },
        { "shipped_shaders_share_one_haze", ShippedShadersShareOneHaze },
        { "shipped_geometry_shaders_share_one_dither", ShippedGeometryShadersShareOneDither },
        { "shipped_skinned_stages_share_one_skinning", ShippedSkinnedStagesShareOneSkinning },
        { "shipped_instanced_twins_share_their_stages", ShippedInstancedTwinsShareTheirStages },
        { "shipped_meter_matches_its_mirror", ShippedMeterMatchesItsMirror },
        { "shipped_depth_readers_share_one_convention", ShippedDepthReadersShareOneConvention },
    };
    TestRun_RunSuite(run, "ShaderSource", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
