#include "TestSuites.h"
#include "TestAssets.h"
#include "TestText.h"
#include "GameFont.h"
#include "FontCodepoints.h"
#include "AssetManager.h"
#include "raylib/raylib.h"
#include "wr/WRFileSystem.h"
#include "wr/WRJSON.h"
#include <string.h>


// Macros.
#define NOTO_SANS_DEFINITION "compile/out/asset/fonts/noto_sans/noto_sans.json"
#define NOTO_SANS_FILE "compile/out/asset/fonts/noto_sans/NotoSans-Regular.ttf"
#define SHIPPED_ASSET_ROOT "compile/out/asset"


// Static functions.
// Parses json and reads its "codepoints" at the given pixel size; *outCodepoints must be freed by the caller.
static Error ReadCodepoints(TestRun* run, const char* json, int pixelSize, int** outCodepoints, int* outCount)
{
    *outCodepoints = NULL;
    *outCount = 0;
    JSONObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        return Error_Construct2(ErrorCode_InvalidState, "the JSON pool could not be created.");
    }
    JSONObjectValue Root;
    Error Result = Error_Construct2(ErrorCode_InvalidJSON, "the test JSON did not parse to an object.");
    if (TestText_ParseJSON(run, Pool, json, &Root))
    {
        if (Root.Type == JSONValueType_Compound)
        {
            Error_Deconstruct(&Result);
            Result = FontCodepoints_Read(Root.Value.Compound, pixelSize, outCodepoints, outCount);
        }
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Root));
    }
    TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    return Result;
}

static bool IsAscendingAndUnique(const int* codepoints, int count)
{
    for (int Index = 1; Index < count; Index++)
    {
        if (codepoints[Index - 1] >= codepoints[Index])
        {
            return false;
        }
    }
    return true;
}

static bool Contains(const int* codepoints, int count, int codepoint)
{
    for (int Index = 0; Index < count; Index++)
    {
        if (codepoints[Index] == codepoint)
        {
            return true;
        }
    }
    return false;
}

static void CheckCodepointFailure(TestRun* run, const char* json, int pixelSize, const char* fragment)
{
    int* Codepoints = NULL;
    int Count = 0;
    const char* const Fragments[] = { fragment, NULL };
    TestText_CheckErrorMessage(run, ReadCodepoints(run, json, pixelSize, &Codepoints, &Count), ErrorCode_InvalidAssetDefinition,
        Fragments);
    TEST_CHECK(run, Codepoints == NULL);
    Memory_Free(Codepoints);
}

static Error BuildFontDefinition(TestAssetFixture* fixture, const char* json)
{
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    GenericBuffer_AppendString(&Bytes, (const unsigned char*)json);
    AssetDefinition* Definition = NULL;
    Error Result = AssetManager_BuildDefinition(fixture->Manager, fixture->Types.Font, &Bytes,
        (const unsigned char*)u8"test font", &Definition);
    if (Definition != NULL)
    {
        Definition->VTable->Destroy(Definition);
    }
    Memory_Free(Bytes._data);
    return Result;
}


// Tests: glyph lookup.
static void GlyphLookupFallsBackToQuestionMark(TestRun* run)
{
    GlyphInfo Glyphs[4] = { { .value = '?', .advanceX = 4 }, { .value = 'A', .advanceX = 6 }, { .value = 'B', .advanceX = 0, .offsetX = 1 },
        { .value = 'a', .advanceX = 5 } };
    Rectangle Recs[4] = { { 0, 0, 4, 10 }, { 4, 0, 6, 10 }, { 10, 0, 7, 10 }, { 17, 0, 5, 10 } };
    Font Sorted = { .baseSize = 10, .glyphCount = 4, .glyphPadding = 0, .recs = Recs, .glyphs = Glyphs };
    GameFont Wrapped = GameFont_Wrap(Sorted);
    TEST_CHECK(run, Wrapped._areGlyphsSorted);
    TEST_CHECK(run, GameFont_FindGlyphIndex(&Wrapped, (CodePoint)'A') == 1);
    TEST_CHECK(run, GameFont_FindGlyphIndex(&Wrapped, (CodePoint)'a') == 3);
    TEST_CHECK(run, GameFont_FindGlyphIndex(&Wrapped, (CodePoint)0x0101) == 0);
    TEST_CHECK(run, GameFont_HasGlyph(&Wrapped, (CodePoint)'B') && !GameFont_HasGlyph(&Wrapped, (CodePoint)'z'));
    // A glyph without an advance advances by its rectangle width plus its offset, as raylib measures it.
    TEST_CHECK(run, GameFont_GetGlyphAdvance(&Wrapped, 2) == 8.0f);

    // Unsorted glyphs are searched linearly.
    GlyphInfo Unsorted[3] = { { .value = 'b' }, { .value = 'a' }, { .value = 'c' } };
    Font Shuffled = { .baseSize = 10, .glyphCount = 3, .recs = Recs, .glyphs = Unsorted };
    GameFont Linear = GameFont_Wrap(Shuffled);
    TEST_CHECK(run, !Linear._areGlyphsSorted);
    TEST_CHECK(run, GameFont_FindGlyphIndex(&Linear, (CodePoint)'a') == 1);
    // No '?' glyph: a missing codepoint draws nothing.
    TEST_CHECK(run, GameFont_FindGlyphIndex(&Linear, (CodePoint)'z') == -1);

    Font EmptyFont = { 0 };
    GameFont Empty = GameFont_Wrap(EmptyFont);
    TEST_CHECK(run, !GameFont_HasGlyphs(&Empty) && (GameFont_FindGlyphIndex(&Empty, (CodePoint)'a') == -1));
}


// Tests: the "codepoints" key.
static void CodepointsDefaultToEuropeanLatin(TestRun* run)
{
    int* Codepoints = NULL;
    int Count = 0;
    if (TEST_CHECK_SUCCESS(run, ReadCodepoints(run, "{}", 32, &Codepoints, &Count)))
    {
        // basic_latin (95) + latin_1_supplement (96) + latin_extended_a (128); '?' and ' ' are in basic_latin.
        TEST_CHECK(run, Count == 319);
        TEST_CHECK(run, (Codepoints[0] == 0x20) && (Codepoints[Count - 1] == 0x17F));
        TEST_CHECK(run, IsAscendingAndUnique(Codepoints, Count));
        TEST_CHECK(run, Contains(Codepoints, Count, 0x101) && Contains(Codepoints, Count, 0x17E));
    }
    Memory_Free(Codepoints);
}

// '?' (drawn for missing codepoints) and the space (whose advance spaces words) are added to every set.
static void CodepointsMergeEntriesAndAddTheFallbackAndSpace(TestRun* run)
{
    int* Codepoints = NULL;
    int Count = 0;
    if (TEST_CHECK_SUCCESS(run, ReadCodepoints(run, "{\"codepoints\": [[65, 67], 66, [64, 65], [\"0x400\", \"0x401\"]]}", 32,
        &Codepoints, &Count)))
    {
        int Expected[] = { ' ', '?', 64, 65, 66, 67, 0x400, 0x401 };
        TEST_CHECK(run, (Count == 8) && (memcmp(Codepoints, Expected, sizeof(Expected)) == 0));
    }
    Memory_Free(Codepoints);

    if (TEST_CHECK_SUCCESS(run, ReadCodepoints(run, "{\"codepoints\": [\"cyrillic\"]}", 32, &Codepoints, &Count)))
    {
        TEST_CHECK(run, (Count == 258) && (Codepoints[0] == ' ') && (Codepoints[1] == '?') && (Codepoints[2] == 0x400)
            && (Codepoints[257] == 0x4FF));
    }
    Memory_Free(Codepoints);
}

static void CodepointsRejectBadEntries(TestRun* run)
{
    CheckCodepointFailure(run, "{\"codepoints\": [\"greek\"]}", 32, "no known block");
    CheckCodepointFailure(run, "{\"codepoints\": [[55295, 57344]]}", 32, "surrogates");
    CheckCodepointFailure(run, "{\"codepoints\": [[70, 65]]}", 32, "first <= last");
    CheckCodepointFailure(run, "{\"codepoints\": [16]}", 32, "U+0020");
    CheckCodepointFailure(run, "{\"codepoints\": \"basic_latin\"}", 32, "must be an array");
    CheckCodepointFailure(run, "{\"codepoints\": [true]}", 32, "entry 0");
    CheckCodepointFailure(run, "{\"codepoints\": [[32, 50000]]}", 32, "at most 8192");
    CheckCodepointFailure(run, "{\"codepoints\": [[256, 2255]]}", 256, "8192 x 8192 atlas");

    // More entries than FONT_CODEPOINTS_MAX_ENTRY_COUNT, even repeating one codepoint, is rejected before sorting.
    GenericBuffer Json;
    GenericBuffer_AllocateVariable(&Json, 0U, sizeof(unsigned char));
    GenericBuffer_AppendString(&Json, (const unsigned char*)u8"{\"codepoints\": [65");
    for (size_t Index = 0U; Index < FONT_CODEPOINTS_MAX_ENTRY_COUNT; Index++)
    {
        GenericBuffer_AppendString(&Json, (const unsigned char*)u8",65");
    }
    GenericBuffer_AppendString(&Json, (const unsigned char*)u8"]}");
    if (TEST_CHECK(run, GenericBuffer_NullTerminate(&Json)))
    {
        CheckCodepointFailure(run, (const char*)Json._data, 32, "at most 1024 are allowed");
    }
    Memory_Free(Json._data);
}

static void FontDefinitionsValidateTheirKeys(TestRun* run)
{
    TestAssetFixture Fixture;
    if (TestAssetFixture_Construct(run, &Fixture))
    {
        TEST_CHECK_SUCCESS(run, BuildFontDefinition(&Fixture, "{\"name\": \"f\", \"location\": \"f/f\", \"size\": 48, "
            "\"codepoints\": [\"basic_latin\"], \"texture_properties\": {\"filtering\": \"point\"}}"));
        TEST_CHECK_ERROR(run, BuildFontDefinition(&Fixture, "{\"name\": \"f\", \"location\": \"f/f\", \"size\": 300}"),
            ErrorCode_InvalidAssetDefinition);
        TEST_CHECK_ERROR(run, BuildFontDefinition(&Fixture, "{\"name\": \"f\", \"location\": \"f/f\", \"size\": 4}"),
            ErrorCode_InvalidAssetDefinition);
        TEST_CHECK_ERROR(run, BuildFontDefinition(&Fixture, "{\"name\": \"f\", \"location\": \"f/f\", \"codepoints\": [\"greek\"]}"),
            ErrorCode_InvalidAssetDefinition);
        TEST_CHECK_ERROR(run, BuildFontDefinition(&Fixture, "{\"name\": \"f\", \"location\": \"f/f\", "
            "\"texture_properties\": {\"filtering\": \"cubic\"}}"), ErrorCode_InvalidAssetDefinition);
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}


// Tests: what the game ships.
static void ShippedDefinitionsAllParse(TestRun* run)
{
    AssetManager* Manager = NULL;
    StandardAssetTypes Types;
    JSONObjectPool* Pool = NULL;
    GenericBuffer Root;
    GenericBuffer_AllocateVariable(&Root, 0U, sizeof(unsigned char));
    if (TEST_CHECK_SUCCESS(run, AssetManager_Construct1(NULL, &Manager))
        && TEST_CHECK_SUCCESS(run, AssetManager_CreateStandardAssetTypes(Manager, &Types, &Pool))
        && TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, SHIPPED_ASSET_ROOT, &Root))
        && TEST_CHECK_SUCCESS(run, AssetManager_AddSearchRoot(Manager, Root._data)))
    {
        size_t FailedFileCount = 1U;
        bool HasNotoSans = false;
        TEST_CHECK_SUCCESS(run, AssetManager_ReadDefinitions(Manager, &FailedFileCount));
        TEST_CHECK(run, FailedFileCount == 0U);
        TEST_CHECK_SUCCESS(run, AssetManager_HasDefinition(Manager, Types.Font, (const unsigned char*)u8"noto_sans", &HasNotoSans));
        TEST_CHECK(run, HasNotoSans);
    }
    if (Manager != NULL)
    {
        TEST_CHECK_SUCCESS(run, AssetManager_Deconstruct(Manager));
    }
    if (Pool != NULL)
    {
        TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    }
    Memory_Free(Root._data);
}

// The shipped Noto Sans file has a glyph for every codepoint its definition asks for (Latvian and Cyrillic included).
// raylib's LoadFontData rasterizes on the CPU, so this runs without a window.
static void ShippedNotoSansCoversItsCodepoints(TestRun* run)
{
    GenericBuffer DefinitionPath;
    GenericBuffer FilePath;
    GenericBuffer DefinitionText;
    GenericBuffer FontBytes;
    GenericBuffer_AllocateVariable(&DefinitionPath, 0U, sizeof(unsigned char));
    GenericBuffer_AllocateVariable(&FilePath, 0U, sizeof(unsigned char));
    GenericBuffer_AllocateVariable(&DefinitionText, 0U, sizeof(unsigned char));
    GenericBuffer_AllocateVariable(&FontBytes, 0U, sizeof(unsigned char));

    int* Codepoints = NULL;
    int Count = 0;
    bool AreFilesRead = TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, NOTO_SANS_DEFINITION, &DefinitionPath))
        && TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, NOTO_SANS_FILE, &FilePath))
        && TEST_CHECK_SUCCESS(run, FileSystem_ReadAllText(DefinitionPath._data, &DefinitionText))
        && TEST_CHECK_SUCCESS(run, FileSystem_ReadAllBytes(FilePath._data, &FontBytes))
        && TEST_CHECK(run, GenericBuffer_NullTerminate(&DefinitionText));
    if (AreFilesRead && TEST_CHECK_SUCCESS(run, ReadCodepoints(run, (const char*)DefinitionText._data, 48, &Codepoints, &Count)))
    {
        static const int Latvian[] = { 0x101, 0x10D, 0x113, 0x123, 0x12B, 0x137, 0x13C, 0x146, 0x161, 0x16B, 0x17E };
        for (size_t Index = 0; Index < (sizeof(Latvian) / sizeof(Latvian[0])); Index++)
        {
            TEST_CHECK(run, Contains(Codepoints, Count, Latvian[Index]));
        }
        TEST_CHECK(run, Contains(Codepoints, Count, 0x44F));

        int GlyphCount = 0;
        GlyphInfo* Glyphs = LoadFontData(FontBytes._data, (int)FontBytes._count, 16, Codepoints, Count, FONT_DEFAULT, &GlyphCount);
        TEST_CHECK(run, Glyphs != NULL);
        TEST_CHECK(run, GlyphCount == Count);
        if (Glyphs != NULL)
        {
            UnloadFontData(Glyphs, GlyphCount);
        }
    }
    Memory_Free(Codepoints);
    Memory_Free(DefinitionPath._data);
    Memory_Free(FilePath._data);
    Memory_Free(DefinitionText._data);
    Memory_Free(FontBytes._data);
}


// Public functions.
void FontTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "glyph_lookup_falls_back_to_question_mark", GlyphLookupFallsBackToQuestionMark },
        { "codepoints_default_to_european_latin", CodepointsDefaultToEuropeanLatin },
        { "codepoints_merge_entries_and_add_the_fallback_and_space", CodepointsMergeEntriesAndAddTheFallbackAndSpace },
        { "codepoints_reject_bad_entries", CodepointsRejectBadEntries },
        { "font_definitions_validate_their_keys", FontDefinitionsValidateTheirKeys },
        { "shipped_definitions_all_parse", ShippedDefinitionsAllParse },
        { "shipped_noto_sans_covers_its_codepoints", ShippedNotoSansCoversItsCodepoints },
    };
    TestRun_RunSuite(run, "Font", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
