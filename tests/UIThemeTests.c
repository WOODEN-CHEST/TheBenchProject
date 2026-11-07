#include "TestSuites.h"
#include "TestText.h"
#include "TestUI.h"
#include "UIBoxStyle.h"
#include "UITheme.h"
#include "Logger.h"
#include "wr/WRFileSystem.h"
#include "wr/WRPath.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
/** A theme naming every kind of value of a button, with one variant. */
#define BUTTON_THEME \
    "{\"name\": \"buttons\", \"font\": \"mono\", \"text_size\": 16, \"transition_time\": 0.25, \"elements\": {" \
    "\"button\": {\"padding\": [8, 4], \"min_size\": [24, 26]," \
    "\"background\": {\"kind\": \"flat\", \"colors\": {\"normal\": \"#3a3a3a\", \"hovered\": \"#4a4a4a\", \"pressed\": \"#2a2a2a\", \"disabled\": \"#303030\"}," \
    "\"outline\": {\"thickness\": 2, \"colors\": {\"normal\": \"#111111\"}}}," \
    "\"text\": {\"normal\": \"#e8e8e8\", \"disabled\": \"#808080\"}," \
    "\"variants\": {\"danger\": {\"background\": {\"colors\": {\"normal\": \"#7a2a2a\"}}}, \"bare\": {\"background\": {\"kind\": \"none\"}, \"min_size\": [1, 2]}}}}}"
/** A theme whose colours come from its palette, in every place a colour can be: a box's colours, its outline's, a colours
 *  part, a custom colour, and a variant's of each. The palette comes after the elements in the text. */
#define PALETTE_THEME \
    "{\"name\": \"painted\", \"elements\": {\"button\": {" \
    "\"background\": {\"kind\": \"flat\", \"colors\": {\"normal\": \"@fill\", \"hovered\": \"#aabbcc\"}, \"outline\": {\"colors\": {\"normal\": \"@edge\"}}}," \
    "\"text\": {\"normal\": \"@ink\", \"disabled\": \"@faded\"}, \"custom\": {\"glow\": \"@glow\"}," \
    "\"variants\": {\"loud\": {\"background\": {\"colors\": {\"normal\": \"@edge\"}}, \"custom\": {\"glow\": \"@ink\"}}}}}," \
    "\"palette\": {\"fill\": \"#102030\", \"edge\": \"#405060\", \"ink\": [1, 2, 3], \"glow\": {\"r\": 7, \"g\": 8, \"b\": 9, \"a\": 10}," \
    "\"faded\": \"#11223344\"}}"
/** A slider with a custom value of every kind, at the bounds too, and a variant that names nothing. */
#define CUSTOM_THEME \
    "{\"name\": \"custom\", \"palette\": {\"gold\": \"#ffd27a\"}, \"elements\": {\"slider\": {\"custom\": {" \
    "\"radius\": 4.5, \"count\": 3, \"lowest\": -1000000, \"highest\": 1000000, \"offset\": [1, -2.5], \"tint\": \"#ff000080\"," \
    "\"gold\": \"@gold\", \"frame\": {\"kind\": \"flat\", \"colors\": {\"normal\": \"@gold\"}, \"outline\": {\"thickness\": 2}}," \
    "\"art\": {\"kind\": \"nine_slice\", \"texture\": \"art\", \"margins\": 2}}," \
    "\"variants\": {\"plain\": {}}}, \"button\": {\"min_size\": [1, 1]}}}"
/** How many bytes a refusal test's JSON may take. */
#define THEME_TEXT_CAPACITY ((size_t)1024)
/** Room for a log's path and text. */
#define LOG_BUFFER_CAPACITY ((size_t)256)
/** Where the game's shipped themes are, from the repository's root. */
#define SHIPPED_THEME_DIRECTORY "compile/out/asset/ui_themes/"


// Types.
/* A definition that must be refused, and what its message must say. */
typedef struct RefusalStruct
{
    const char* Json;
    const char* MessagePart;
} Refusal;


// Static functions.
static Color Rgb(unsigned char r, unsigned char g, unsigned char b)
{
    return (Color){ .r = r, .g = g, .b = b, .a = 255U };
}

static bool IsColor(Color color, Color expected)
{
    return (color.r == expected.r) && (color.g == expected.g) && (color.b == expected.b) && (color.a == expected.a);
}

static Error BuildTheme(TestAssetFixture* fixture, const char* json)
{
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    GenericBuffer_AppendString(&Bytes, (const unsigned char*)json);
    AssetDefinition* Definition = NULL;
    Error Result = AssetManager_BuildDefinition(fixture->Manager, fixture->Types.UITheme, &Bytes, (const unsigned char*)u8"test", &Definition);
    if (Definition != NULL)
    {
        Definition->VTable->Destroy(Definition);
    }
    Memory_Free(Bytes._data);
    return Result;
}

/* Registers a theme and loads it under a new user. */
static const UITheme* LoadTheme(TestRun* run, TestAssetFixture* fixture, const char* json, const char* name, AssetUserID* outUser)
{
    UITheme* Theme = NULL;
    bool IsLoaded = TestAssets_AddUITheme(run, fixture, json)
        && TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(fixture->Manager, outUser))
        && TEST_CHECK_SUCCESS(run, AssetManager_LoadUITheme(fixture->Manager, (const unsigned char*)name, *outUser, &Theme));
    return IsLoaded ? Theme : NULL;
}

/* The nine quads, checked against a box and the art: every quad's edges shared, every outer edge the box's. */
static bool DoQuadsTileTheBox(const UINineSliceQuad* quads, Rectangle box)
{
    for (size_t Row = 0U; Row < 3U; Row++)
    {
        for (size_t Column = 0U; Column < 3U; Column++)
        {
            const Rectangle* Quad = &quads[(Row * 3U) + Column].Destination;
            if ((Quad->width < 0.0f) || (Quad->height < 0.0f))
            {
                return false;
            }
            if ((Column > 0U) && (quads[(Row * 3U) + Column - 1U].Destination.x + quads[(Row * 3U) + Column - 1U].Destination.width != Quad->x))
            {
                return false; // A gap or an overlap to its left neighbour.
            }
            if ((Row > 0U) && (quads[((Row - 1U) * 3U) + Column].Destination.y + quads[((Row - 1U) * 3U) + Column].Destination.height != Quad->y))
            {
                return false; // A gap or an overlap to its upper neighbour.
            }
        }
        const Rectangle* First = &quads[Row * 3U].Destination;
        const Rectangle* Last = &quads[(Row * 3U) + 2U].Destination;
        if ((First->x != box.x) || ((Last->x + Last->width) != (box.x + box.width)))
        {
            return false;
        }
    }
    for (size_t Column = 0U; Column < 3U; Column++)
    {
        const Rectangle* Top = &quads[Column].Destination;
        const Rectangle* Bottom = &quads[6U + Column].Destination;
        if ((Top->y != box.y) || ((Bottom->y + Bottom->height) != (box.y + box.height)))
        {
            return false;
        }
    }
    return true;
}

/* Builds every definition of a table, each of which must be refused with its message. */
static void CheckRefusals(TestRun* run, const Refusal* refusals, size_t count)
{
    TestAssetFixture Fixture;
    if (!TestAssetFixture_Construct(run, &Fixture))
    {
        return;
    }
    for (size_t Index = 0U; Index < count; Index++)
    {
        const char* Parts[] = { refusals[Index].MessagePart, NULL };
        Error Result = BuildTheme(&Fixture, refusals[Index].Json);
        if (!TestText_CheckErrorMessage(run, Result, ErrorCode_InvalidAssetDefinition, Parts))
        {
            printf("    in refusal %zu: %s\n", Index, refusals[Index].Json);
        }
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

/* A button whose kind has @p kindCount custom numbers ("k0", ...) and a variant that names @p overrideCount of them again
 * and adds @p addCount of its own ("n0", ...). */
static void WriteMergedCustomTheme(char* text, size_t capacity, int kindCount, int overrideCount, int addCount)
{
    int Length = snprintf(text, capacity, "{\"name\": \"t\", \"elements\": {\"button\": {\"custom\": {");
    for (int Index = 0; Index < kindCount; Index++)
    {
        Length += snprintf(text + Length, capacity - (size_t)Length, "%s\"k%d\": %d", (Index > 0) ? ", " : "", Index, Index);
    }
    Length += snprintf(text + Length, capacity - (size_t)Length, "}, \"variants\": {\"v\": {\"custom\": {");
    for (int Index = 0; Index < (overrideCount + addCount); Index++)
    {
        bool IsOverride = Index < overrideCount;
        Length += snprintf(text + Length, capacity - (size_t)Length, "%s\"%c%d\": 100", (Index > 0) ? ", " : "",
            IsOverride ? 'k' : 'n', IsOverride ? Index : (Index - overrideCount));
    }
    snprintf(text + Length, capacity - (size_t)Length, "}}}}}}");
}

/* Reads a shipped theme's file ("compile/out/asset/ui_themes/<file>") into @p outText, NUL-terminated. */
static bool ReadShippedTheme(TestRun* run, const char* file, GenericBuffer* outText)
{
    char Relative[LOG_BUFFER_CAPACITY];
    snprintf(Relative, sizeof(Relative), "%s%s", SHIPPED_THEME_DIRECTORY, file);
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, 0U, sizeof(unsigned char));
    bool IsRead = TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, Relative, &Path))
        && TEST_CHECK_SUCCESS(run, FileSystem_ReadAllText(Path._data, outText));
    Memory_Free(Path._data);
    return IsRead;
}

/* The font and the art the shipped themes name, built in code so they load without a window (16 x 16 textures). */
static bool AddShippedThemeDependencies(TestRun* run, TestAssetFixture* fixture)
{
    return TestAssets_AddFont(run, fixture, "ui_small", 5, NULL) && TestAssets_AddTexture(run, fixture, "ui_box", 11U, NULL)
        && TestAssets_AddTexture(run, fixture, "ui_check", 12U, NULL) && TestAssets_AddTexture(run, fixture, "ui_track", 13U, NULL)
        && TestAssets_AddTexture(run, fixture, "ui_knob", 14U, NULL);
}

/* The colour a part of a kind's style (or of one of its variants) shows in a state: a box's fill or a colours part's. */
static Color PartColor(const UITheme* theme, UIElementKind kind, const char* variant, int32_t part, UIElementState state)
{
    const UIThemeStyle* Style = UITheme_FindStyle(theme, kind, (const unsigned char*)variant);
    const UIBoxStyle* Box = UIThemeStyle_GetBox(Style, part);
    if (Box != NULL)
    {
        return UIBoxStyle_Resolve(Box, state).Fill;
    }
    const UIStateColors* Colors = UIThemeStyle_GetColors(Style, part);
    return (Colors != NULL) ? UIStateColors_Resolve(Colors, state, BLANK) : BLANK;
}

/* Counts the occurrences of @p needle in "<directory>/logs/latest.log". */
static size_t CountInLog(TestRun* run, const unsigned char* directory, const char* needle)
{
    size_t Count = 0U;
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, LOG_BUFFER_CAPACITY, sizeof(unsigned char));
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, LOG_BUFFER_CAPACITY, sizeof(unsigned char));
    if (TEST_CHECK_SUCCESS(run, Path_Append(directory, (const unsigned char*)u8"logs/latest.log", &Path))
        && TEST_CHECK_SUCCESS(run, FileSystem_ReadAllBytes(Path._data, &Text)) && TEST_CHECK(run, GenericBuffer_NullTerminate(&Text)))
    {
        for (const char* Found = strstr((const char*)Text._data, needle); Found != NULL; Found = strstr(Found + 1, needle))
        {
            Count++;
        }
    }
    Memory_Free(Path._data);
    Memory_Free(Text._data);
    return Count;
}


// Tests: reading.
static void AMinimalThemeGetsTheDefaults(TestRun* run)
{
    TestAssetFixture Fixture;
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (TestAssetFixture_Construct(run, &Fixture))
    {
        const UITheme* Theme = LoadTheme(run, &Fixture, "{\"name\": \"bare\"}", "bare", &User);
        if (TEST_CHECK(run, Theme != NULL))
        {
            TEST_CHECK(run, strcmp((const char*)UITheme_GetName(Theme), "tbp:bare") == 0);
            TEST_CHECK(run, UITheme_GetFontName(Theme) == NULL);
            TEST_CHECK(run, UITheme_GetTextSize(Theme) == UI_THEME_DEFAULT_TEXT_SIZE);
            TEST_CHECK(run, UITheme_GetTransitionSeconds(Theme) == 0.0f);
            TEST_CHECK(run, UITheme_GetTooltipDelaySeconds(Theme) == UI_THEME_DEFAULT_TOOLTIP_DELAY_SECONDS);
            TEST_CHECK(run, UITheme_FindStyle(Theme, UIElementKind_Button, NULL) == NULL);
            TEST_CHECK(run, UITheme_GetLookName(Theme, UIElementKind_Button) == NULL);
        }
        TestAssetFixture_Deconstruct(run, &Fixture);
    }
}

static void NoThemeAtAllAnswersWithTheDefaults(TestRun* run)
{
    TEST_CHECK(run, UITheme_GetFontName(NULL) == NULL);
    TEST_CHECK(run, UITheme_GetTextSize(NULL) == UI_THEME_DEFAULT_TEXT_SIZE);
    TEST_CHECK(run, UITheme_GetTransitionSeconds(NULL) == 0.0f);
    TEST_CHECK(run, UITheme_GetTooltipDelaySeconds(NULL) == UI_THEME_DEFAULT_TOOLTIP_DELAY_SECONDS);
    TEST_CHECK(run, UITheme_GetLookName(NULL, UIElementKind_Button) == NULL);
    TEST_CHECK(run, UITheme_FindStyle(NULL, UIElementKind_Button, NULL) == NULL);
    TEST_CHECK(run, UIThemeStyle_FindCustomNumber(NULL, u8"radius", 2.0f) == 2.0f);
    TEST_CHECK(run, UIThemeStyle_FindCustomBox(NULL, u8"frame") == NULL);
    TEST_CHECK(run, UIThemeStyle_GetBox(NULL, UIButtonPart_Background) == NULL);
    TEST_CHECK(run, UIThemeStyle_GetColors(NULL, UIButtonPart_Text) == NULL);
    Vector2 Padding = UIThemeStyle_GetVector(NULL, UIElementKind_Button, UIButtonPart_Padding);
    TEST_CHECK(run, (Padding.x == 8.0f) && (Padding.y == 4.0f));
    TEST_CHECK(run, UIThemeStyle_GetNumber(NULL, UIElementKind_Slider, UISliderPart_TrackThickness) == 4.0f);

    // Asking for a value of the wrong shape, or a kind or a part that does not exist, gives nothing.
    Vector2 NotAVector = UIThemeStyle_GetVector(NULL, UIElementKind_Button, UIButtonPart_Background);
    TEST_CHECK(run, (NotAVector.x == 0.0f) && (NotAVector.y == 0.0f));
    TEST_CHECK(run, UIThemeStyle_GetNumber(NULL, UIElementKind_Button, UIButtonPart_Padding) == 0.0f);
    TEST_CHECK(run, UIElementKind_GetName((UIElementKind)99) == NULL);
    TEST_CHECK(run, UIElementKind_GetPartCount((UIElementKind)99) == 0U);
    TEST_CHECK(run, UIElementKind_GetPart(UIElementKind_Button, 99) == NULL);
    TEST_CHECK(run, UIElementKind_GetPart(UIElementKind_Button, -1) == NULL);
    TEST_CHECK(run, UIElementState_GetName(UIElementState_Count) == NULL);
}

static void EveryKeyOfAButtonIsRead(TestRun* run)
{
    TestAssetFixture Fixture;
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (TestAssetFixture_Construct(run, &Fixture))
    {
        TEST_CHECK(run, TestAssets_AddFont(run, &Fixture, "mono", 5, NULL));
        const UITheme* Theme = LoadTheme(run, &Fixture, BUTTON_THEME, "buttons", &User);
        if (TEST_CHECK(run, Theme != NULL))
        {
            TEST_CHECK(run, strcmp((const char*)UITheme_GetFontName(Theme), "tbp:mono") == 0);
            TEST_CHECK(run, UITheme_GetTextSize(Theme) == 16.0f);
            TEST_CHECK(run, UITheme_GetTransitionSeconds(Theme) == 0.25f);

            const UIThemeStyle* Style = UITheme_FindStyle(Theme, UIElementKind_Button, NULL);
            Vector2 Padding = UIThemeStyle_GetVector(Style, UIElementKind_Button, UIButtonPart_Padding);
            Vector2 Minimum = UIThemeStyle_GetVector(Style, UIElementKind_Button, UIButtonPart_MinSize);
            TEST_CHECK(run, (Padding.x == 8.0f) && (Padding.y == 4.0f) && (Minimum.x == 24.0f) && (Minimum.y == 26.0f));

            const UIBoxStyle* Box = UIThemeStyle_GetBox(Style, UIButtonPart_Background);
            if (TEST_CHECK(run, Box != NULL))
            {
                TEST_CHECK(run, Box->Kind == UIBoxKind_Flat);
                TEST_CHECK(run, Box->HasOutline && (Box->OutlineThickness == 2.0f));
                TEST_CHECK(run, IsColor(UIBoxStyle_Resolve(Box, UIElementState_Normal).Fill, Rgb(0x3a, 0x3a, 0x3a)));
                TEST_CHECK(run, IsColor(UIBoxStyle_Resolve(Box, UIElementState_Hovered).Fill, Rgb(0x4a, 0x4a, 0x4a)));
                TEST_CHECK(run, IsColor(UIBoxStyle_Resolve(Box, UIElementState_Pressed).Fill, Rgb(0x2a, 0x2a, 0x2a)));
                TEST_CHECK(run, IsColor(UIBoxStyle_Resolve(Box, UIElementState_Disabled).Fill, Rgb(0x30, 0x30, 0x30)));
                TEST_CHECK(run, IsColor(UIBoxStyle_Resolve(Box, UIElementState_Normal).Outline, Rgb(0x11, 0x11, 0x11)));
            }
            const UIStateColors* Text = UIThemeStyle_GetColors(Style, UIButtonPart_Text);
            if (TEST_CHECK(run, Text != NULL))
            {
                TEST_CHECK(run, IsColor(UIStateColors_Resolve(Text, UIElementState_Disabled, WHITE), Rgb(0x80, 0x80, 0x80)));
            }
        }
        TestAssetFixture_Deconstruct(run, &Fixture);
    }
}

/* A state a theme leaves out falls back to normal; with no normal either, to what the caller says. */
static void AStateLeftOutFallsBackToNormal(TestRun* run)
{
    TestAssetFixture Fixture;
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (TestAssetFixture_Construct(run, &Fixture))
    {
        TEST_CHECK(run, TestAssets_AddFont(run, &Fixture, "mono", 5, NULL));
        const UITheme* Theme = LoadTheme(run, &Fixture, BUTTON_THEME, "buttons", &User);
        if (TEST_CHECK(run, Theme != NULL))
        {
            const UIBoxStyle* Box = UIThemeStyle_GetBox(UITheme_FindStyle(Theme, UIElementKind_Button, NULL), UIButtonPart_Background);
            const UIElementState LeftOut[] = { UIElementState_Focused, UIElementState_Selected, UIElementState_Invalid };
            for (size_t Index = 0U; Index < (sizeof(LeftOut) / sizeof(LeftOut[0])); Index++)
            {
                TEST_CHECK(run, IsColor(UIBoxStyle_Resolve(Box, LeftOut[Index]).Fill, Rgb(0x3a, 0x3a, 0x3a)));
            }
            // A state out of range counts as normal.
            TEST_CHECK(run, IsColor(UIBoxStyle_Resolve(Box, (UIElementState)99).Fill, Rgb(0x3a, 0x3a, 0x3a)));
        }
        TestAssetFixture_Deconstruct(run, &Fixture);
    }
    UIStateColors None;
    Memory_Zero(&None, sizeof(None));
    TEST_CHECK(run, IsColor(UIStateColors_Resolve(&None, UIElementState_Hovered, RED), RED));
}

/* A variant changes only what it names, down to one state's colour; an unknown one is the kind's own look. */
static void AVariantChangesOnlyWhatItNames(TestRun* run)
{
    TestAssetFixture Fixture;
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (TestAssetFixture_Construct(run, &Fixture))
    {
        TEST_CHECK(run, TestAssets_AddFont(run, &Fixture, "mono", 5, NULL));
        const UITheme* Theme = LoadTheme(run, &Fixture, BUTTON_THEME, "buttons", &User);
        if (TEST_CHECK(run, Theme != NULL))
        {
            const UIThemeStyle* Base = UITheme_FindStyle(Theme, UIElementKind_Button, NULL);
            const UIThemeStyle* Danger = UITheme_FindStyle(Theme, UIElementKind_Button, u8"danger");
            const UIBoxStyle* DangerBox = UIThemeStyle_GetBox(Danger, UIButtonPart_Background);
            TEST_CHECK(run, Danger != Base);
            TEST_CHECK(run, IsColor(UIBoxStyle_Resolve(DangerBox, UIElementState_Normal).Fill, Rgb(0x7a, 0x2a, 0x2a)));
            TEST_CHECK(run, IsColor(UIBoxStyle_Resolve(DangerBox, UIElementState_Hovered).Fill, Rgb(0x4a, 0x4a, 0x4a)));
            TEST_CHECK(run, DangerBox->HasOutline && (DangerBox->OutlineThickness == 2.0f));
            TEST_CHECK(run, UIThemeStyle_GetColors(Danger, UIButtonPart_Text) != NULL);
            TEST_CHECK(run, UIThemeStyle_GetVector(Danger, UIElementKind_Button, UIButtonPart_MinSize).y == 26.0f);

            // The kind's own look did not change with it.
            const UIBoxStyle* BaseBox = UIThemeStyle_GetBox(Base, UIButtonPart_Background);
            TEST_CHECK(run, IsColor(UIBoxStyle_Resolve(BaseBox, UIElementState_Normal).Fill, Rgb(0x3a, 0x3a, 0x3a)));

            const UIThemeStyle* Bare = UITheme_FindStyle(Theme, UIElementKind_Button, u8"bare");
            TEST_CHECK(run, UIThemeStyle_GetBox(Bare, UIButtonPart_Background)->Kind == UIBoxKind_None);
            TEST_CHECK(run, UIThemeStyle_GetVector(Bare, UIElementKind_Button, UIButtonPart_MinSize).x == 1.0f);
            TEST_CHECK(run, UIThemeStyle_GetVector(Bare, UIElementKind_Button, UIButtonPart_Padding).x == 8.0f);

            TEST_CHECK(run, UITheme_FindStyle(Theme, UIElementKind_Button, u8"nobody") == Base);
            TEST_CHECK(run, UITheme_FindStyle(Theme, UIElementKind_Button, u8"") == Base);
        }
        TestAssetFixture_Deconstruct(run, &Fixture);
    }
}

/* An empty block is no variants: the kind's own look answers, and the loaded theme holds no list of the definition's. */
static void AnEmptyVariantsBlockIsNoVariants(TestRun* run)
{
    TestAssetFixture Fixture;
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (TestAssetFixture_Construct(run, &Fixture))
    {
        const UITheme* Theme = LoadTheme(run, &Fixture, "{\"name\": \"t\", \"elements\": {\"button\": {\"min_size\": [3, 4], \"variants\": {}}}}", "t", &User);
        if (TEST_CHECK(run, Theme != NULL))
        {
            const UIThemeStyle* Base = UITheme_FindStyle(Theme, UIElementKind_Button, NULL);
            TEST_CHECK(run, UITheme_FindStyle(Theme, UIElementKind_Button, u8"any") == Base);
            TEST_CHECK(run, UIThemeStyle_GetVector(Base, UIElementKind_Button, UIButtonPart_MinSize).x == 3.0f);
        }
        TestAssetFixture_Deconstruct(run, &Fixture);
    }
}

/* Asked for a part as what it is not, a style says it was left out: never another kind's bytes. */
static void APartOfAnotherKindCountsAsLeftOut(TestRun* run)
{
    TestAssetFixture Fixture;
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (TestAssetFixture_Construct(run, &Fixture))
    {
        TEST_CHECK(run, TestAssets_AddFont(run, &Fixture, "mono", 5, NULL));
        const UITheme* Theme = LoadTheme(run, &Fixture, BUTTON_THEME, "buttons", &User);
        if (TEST_CHECK(run, Theme != NULL))
        {
            const UIThemeStyle* Style = UITheme_FindStyle(Theme, UIElementKind_Button, NULL);
            TEST_CHECK(run, UIThemeStyle_GetBox(Style, UIButtonPart_Background) != NULL);
            TEST_CHECK(run, UIThemeStyle_GetColors(Style, UIButtonPart_Text) != NULL);
            TEST_CHECK(run, UIThemeStyle_GetBox(Style, UIButtonPart_Text) == NULL);
            TEST_CHECK(run, UIThemeStyle_GetBox(Style, UIButtonPart_Padding) == NULL);
            TEST_CHECK(run, UIThemeStyle_GetColors(Style, UIButtonPart_Background) == NULL);
            TEST_CHECK(run, UIThemeStyle_GetColors(Style, UIButtonPart_MinSize) == NULL);
        }
        TestAssetFixture_Deconstruct(run, &Fixture);
    }
}

static void EveryRuleIsRefusedWithItsMessage(TestRun* run)
{
    static const Refusal Refusals[] =
    {
        { "{}", "name" },
        { "{\"name\": \"t\", \"colour\": 1}", "colour" },
        { "{\"name\": \"t\", \"font\": 5}", "t" },
        { "{\"name\": \"t\", \"font\": \"Not A Name\"}", "UI theme \"tbp:t\"" },
        { "{\"name\": \"t\", \"text_size\": 3}", "outside" },
        { "{\"name\": \"t\", \"text_size\": 257}", "outside" },
        { "{\"name\": \"t\", \"text_size\": \"big\"}", "UI theme" },
        { "{\"name\": \"t\", \"transition_time\": -1}", "outside" },
        { "{\"name\": \"t\", \"transition_time\": 6}", "outside" },
        { "{\"name\": \"t\", \"elements\": 1}", "elements: must be a compound" },
        { "{\"name\": \"t\", \"elements\": {\"spinner\": {}}}", "spinner" },
        { "{\"name\": \"t\", \"elements\": {\"button\": 1}}", "elements.button: must be a compound" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"thumb\": {}}}}", "thumb" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"padding\": 3}}}", "elements.button.padding" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"padding\": [-1, 0]}}}", "elements.button.padding: each component" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"min_size\": [0, 5000]}}}", "elements.button.min_size: each component" },
        { "{\"name\": \"t\", \"elements\": {\"panel\": {\"spacing\": -1}}}", "elements.panel.spacing" },
        { "{\"name\": \"t\", \"elements\": {\"panel\": {\"spacing\": 5000}}}", "outside" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"background\": 1}}}", "elements.button.background: must be a compound" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"background\": {}}}}", "needs a \"kind\"" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"background\": {\"kind\": \"round\"}}}}", "its \"kind\" must be" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"background\": {\"kind\": 5}}}}", "its \"kind\" must be" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"background\": {\"kind\": \"flat\", \"shine\": 1}}}}", "shine" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"background\": {\"kind\": \"flat\", \"colors\": 1}}}}", "must be a compound" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"background\": {\"kind\": \"flat\", \"colors\": {\"glowing\": \"#fff\"}}}}}", "unknown state \"glowing\"" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"background\": {\"kind\": \"flat\", \"colors\": {\"normal\": \"#ggg\"}}}}}", "elements.button.background" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"text\": 1}}}", "elements.button.text: must be a compound" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"background\": {\"kind\": \"none\", \"outline\": {}}}}}", "only a \"flat\" box" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"background\": {\"kind\": \"flat\", \"outline\": 1}}}}", "must be a compound" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"background\": {\"kind\": \"flat\", \"outline\": {\"thickness\": 65}}}}}", "outside" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"background\": {\"kind\": \"flat\", \"outline\": {\"tint\": 1}}}}}", "tint" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"background\": {\"kind\": \"flat\", \"texture\": \"a\"}}}}", "only a \"nine_slice\" box" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"background\": {\"kind\": \"flat\", \"margins\": 2}}}}", "only a \"nine_slice\" box" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"background\": {\"kind\": \"nine_slice\"}}}}", "needs a \"texture\" and \"margins\"" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"background\": {\"kind\": \"nine_slice\", \"texture\": \"a\"}}}}", "needs a \"texture\" and \"margins\"" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"background\": {\"kind\": \"nine_slice\", \"margins\": 2}}}}", "needs a \"texture\" and \"margins\"" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"background\": {\"kind\": \"nine_slice\", \"texture\": \"Bad Name\", \"margins\": 2}}}}", "elements.button.background" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"background\": {\"kind\": \"nine_slice\", \"texture\": 1, \"margins\": 2}}}}", "elements.button.background" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"background\": {\"kind\": \"nine_slice\", \"texture\": \"a\", \"margins\": -2}}}}", "outside" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"background\": {\"kind\": \"nine_slice\", \"texture\": \"a\", \"margins\": [1, 2, 3]}}}}", "must be one number or four" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"background\": {\"kind\": \"nine_slice\", \"texture\": \"a\", \"margins\": [1, 2, 3, 99999]}}}}", "outside" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"background\": {\"kind\": \"nine_slice\", \"texture\": \"a\", \"margins\": 2, \"pixel_scale\": 0}}}}", "outside" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"background\": {\"kind\": \"nine_slice\", \"texture\": \"a\", \"margins\": 2, \"source\": 1}}}}", "must be a compound" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"background\": {\"kind\": \"nine_slice\", \"texture\": \"a\", \"margins\": 2, \"source\": {\"x\": 0, \"y\": 0, \"width\": 8}}}}}", "needs its x, y, width and height" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"background\": {\"kind\": \"nine_slice\", \"texture\": \"a\", \"margins\": 2, \"source\": {\"x\": 0, \"y\": 0, \"width\": 0, \"height\": 8}}}}}", "must be above 0" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"background\": {\"kind\": \"nine_slice\", \"texture\": \"a\", \"margins\": 2, \"source\": {\"x\": 0, \"y\": 0, \"width\": 8, \"height\": 8, \"z\": 1}}}}}", "z" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"background\": {\"kind\": \"nine_slice\", \"texture\": \"a\", \"margins\": 2, \"sources\": {\"loud\": {}}}}}}", "unknown state \"loud\"" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"background\": {\"kind\": \"nine_slice\", \"texture\": \"a\", \"margins\": 2, \"sources\": 3}}}}", "must be a compound" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"variants\": 1}}}", "variants: must be a compound" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"variants\": {\"Bad Name\": {}}}}}", "a variant's name" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"variants\": {\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\": {}}}}}", "a variant's name" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"variants\": {\"a\": 1}}}}", "variants.a: must be a compound" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"variants\": {\"a\": {\"variants\": {}}}}}}", "variants" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"variants\": {\"a\": {\"background\": {\"colors\": {}}}}}}}", "needs a \"kind\"" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"background\": {\"kind\": \"flat\"}, \"variants\": {\"a\": {\"background\": {\"kind\": \"nine_slice\"}}}}}}", "needs a \"texture\" and \"margins\"" },
        { "{\"name\": \"t\", \"tooltip_delay\": -0.5}", "UI theme \"tbp:t\", tooltip_delay: -0.5 is outside [0, 10]" },
        { "{\"name\": \"t\", \"tooltip_delay\": 10.5}", "tooltip_delay: 10.5 is outside" },
        { "{\"name\": \"t\", \"tooltip_delay\": \"soon\"}", "tooltip_delay" },
        { "{\"name\": \"t\", \"elements\": {\"tooltip\": {\"thumb\": {}}}}", "thumb" },
        { "{\"name\": \"t\", \"elements\": {\"tooltip\": {\"max_width\": 5000}}}", "elements.tooltip.max_width: 5000 is outside" },
        { "{\"name\": \"t\", \"elements\": {\"tooltip\": {\"max_width\": -1}}}", "elements.tooltip.max_width: -1 is outside [0, 4096]" },
        { "{\"name\": \"t\", \"elements\": {\"tooltip\": {\"offset\": [-5000, 0]}}}", "elements.tooltip.offset: each component must be within [-4096, 4096]" },
        { "{\"name\": \"t\", \"elements\": {\"tooltip\": {\"padding\": [-1, 0]}}}", "elements.tooltip.padding: each component must be within [0, 4096]" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"min_size\": [0, -1]}}}", "elements.button.min_size: each component must be within [0, 4096]" },
        { "{\"name\": \"t\", \"elements\": {\"tooltip\": {\"background\": {}}}}", "elements.tooltip.background: it needs a \"kind\"" },
    };
    CheckRefusals(run, Refusals, sizeof(Refusals) / sizeof(Refusals[0]));
}

/* A palette's own rules, and a reference that cannot be resolved, refused with where it is. */
static void PaletteRulesAreRefusedNamingWhere(TestRun* run)
{
    static const Refusal Refusals[] =
    {
        { "{\"name\": \"t\", \"palette\": 1}", "UI theme \"tbp:t\", palette: must be a compound" },
        { "{\"name\": \"t\", \"palette\": {\"Bad\": \"#000000\"}}", "palette.Bad: a palette colour's name" },
        { "{\"name\": \"t\", \"palette\": {\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\": \"#000000\"}}", "a palette colour's name" },
        { "{\"name\": \"t\", \"palette\": {\"a\": \"#000000\", \"b\": \"@a\"}}", "palette.b: a palette colour is a colour, never a reference" },
        { "{\"name\": \"t\", \"palette\": {\"a\": \"@a\"}}", "palette.a: a palette colour is a colour, never a reference" },
        { "{\"name\": \"t\", \"palette\": {\"a\": \"#ggg\"}}", "palette.a" },
        { "{\"name\": \"t\", \"palette\": {\"a\": true}}", "palette.a" },
        { "{\"name\": \"t\", \"palette\": {\"a\": \"#000000\"}, \"elements\": {\"button\": {\"text\": {\"normal\": \"@b\"}}}}",
            "elements.button.text.normal: \"@b\" names no colour of the theme's palette" },
        { "{\"name\": \"t\", \"elements\": {\"label\": {\"text\": {\"hovered\": \"@a\"}}}}",
            "elements.label.text.hovered: \"@a\" names a palette colour, but the theme has no \"palette\"" },
        { "{\"name\": \"t\", \"palette\": {}, \"elements\": {\"label\": {\"text\": {\"normal\": \"@a\"}}}}", "\"@a\" names no colour" },
        { "{\"name\": \"t\", \"palette\": {\"a\": \"#000000\"}, \"elements\": {\"label\": {\"text\": {\"normal\": \"@\"}}}}", "\"@\" names no colour" },
        { "{\"name\": \"t\", \"palette\": {\"a\": \"#000000\"}, \"elements\": {\"label\": {\"text\": {\"normal\": \"@A\"}}}}", "\"@A\" names no colour" },
        { "{\"name\": \"t\", \"palette\": {\"a\": \"#000000\"}, \"elements\": {\"button\": {\"background\": {\"kind\": \"flat\", \"colors\": {\"pressed\": \"@z\"}}}}}",
            "elements.button.background.colors.pressed: \"@z\"" },
        { "{\"name\": \"t\", \"palette\": {\"a\": \"#000000\"}, \"elements\": {\"panel\": {\"background\": {\"kind\": \"flat\", \"outline\": {\"colors\": {\"hovered\": \"@z\"}}}}}}",
            "elements.panel.background.outline.colors.hovered: \"@z\"" },
        { "{\"name\": \"t\", \"palette\": {\"a\": \"#000000\"}, \"elements\": {\"button\": {\"custom\": {\"glow\": \"@z\"}}}}",
            "elements.button.custom.glow: \"@z\"" },
        { "{\"name\": \"t\", \"palette\": {\"a\": \"#000000\"}, \"elements\": {\"button\": {\"variants\": {\"v\": {\"text\": {\"normal\": \"@z\"}}}}}}",
            "elements.button.variants.v.text.normal: \"@z\"" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"background\": {\"kind\": \"flat\", \"colors\": {\"glowing\": \"@a\"}}}}}",
            "elements.button.background.colors: unknown state \"glowing\"" },
    };
    CheckRefusals(run, Refusals, sizeof(Refusals) / sizeof(Refusals[0]));
}

/* Custom values of no known form, out of bounds or under a bad key, refused with where they are. */
static void CustomRulesAreRefusedNamingWhere(TestRun* run)
{
    static const Refusal Refusals[] =
    {
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"custom\": 1}}}", "elements.button.custom: must be a compound" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"custom\": {\"Bad\": 1}}}}", "elements.button.custom.Bad: a custom value's key" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"custom\": {\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\": 1}}}}", "a custom value's key" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"custom\": {\"1st\": 1}}}}", "a custom value's key" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"custom\": {\"x\": 1000001}}}}", "elements.button.custom.x: 1e+06 is outside" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"custom\": {\"x\": -1000001}}}}", "elements.button.custom.x: -1e+06 is outside" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"custom\": {\"x\": 1e39}}}}", "elements.button.custom.x" }, // no finite float
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"custom\": {\"x\": [0, 2000000]}}}}", "elements.button.custom.x: 2e+06 is outside" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"custom\": {\"x\": [1e39, 0]}}}}", "elements.button.custom.x" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"custom\": {\"x\": [1, 2, 3]}}}}", "elements.button.custom.x: a custom vector is an array of exactly two numbers" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"custom\": {\"x\": [1]}}}}", "exactly two numbers" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"custom\": {\"x\": []}}}}", "exactly two numbers" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"custom\": {\"x\": [\"1\", \"2\"]}}}}", "exactly two numbers" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"custom\": {\"x\": [[1], 2]}}}}", "exactly two numbers" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"custom\": {\"x\": true}}}}", "elements.button.custom.x: a custom value is a number" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"custom\": {\"x\": null}}}}", "a custom value is a number" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"custom\": {\"x\": \"red\"}}}}", "elements.button.custom.x" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"custom\": {\"x\": \"12\"}}}}", "elements.button.custom.x" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"custom\": {\"x\": {}}}}}", "elements.button.custom.x: it needs a \"kind\"" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"custom\": {\"x\": {\"kind\": \"flat\", \"shine\": 1}}}}}", "shine" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"custom\": {\"x\": {\"kind\": \"nine_slice\", \"margins\": 2}}}}}", "needs a \"texture\" and \"margins\"" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"custom\": {\"x\": {\"kind\": \"none\", \"outline\": {}}}}}}", "only a \"flat\" box" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"custom\": {\"x\": {\"kind\": \"flat\", \"colors\": {\"normal\": \"#ggg\"}}}}}}",
            "elements.button.custom.x.colors.normal" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"variants\": {\"v\": {\"custom\": {\"x\": false}}}}}}", "elements.button.variants.v.custom.x" },
        // A variant's entry replaces its kind's whole: a box there is read anew and needs its own kind.
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"custom\": {\"f\": {\"kind\": \"flat\"}}, \"variants\": {\"v\": {\"custom\": {\"f\": {\"colors\": {}}}}}}}}",
            "elements.button.variants.v.custom.f: it needs a \"kind\"" },
        // The look's key belongs to a kind's own block.
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"look\": 5}}}", "elements.button.look" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"look\": \"Not A Name\"}}}", "elements.button.look" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"look\": \"\"}}}", "elements.button.look" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"look\": \"a:\"}}}", "elements.button.look" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"variants\": {\"v\": {\"look\": \"round\"}}}}}",
            "elements.button.variants.v.look: only a kind's own block names a look" },
        { "{\"name\": \"t\", \"elements\": {\"button\": {\"variants\": {\"v\": {\"look\": 1, \"min_size\": [1, 1]}}}}}", "only a kind's own block names a look" },
    };
    CheckRefusals(run, Refusals, sizeof(Refusals) / sizeof(Refusals[0]));
}

/* Numbers and rectangles at their limits read; a file past them is refused however many it holds. */
static void HostileSizesAndCountsAreRefused(TestRun* run)
{
    TestAssetFixture Fixture;
    if (!TestAssetFixture_Construct(run, &Fixture))
    {
        return;
    }
    char Text[THEME_TEXT_CAPACITY * 8U];

    // Sixteen variants read, seventeen do not.
    for (int Count = 16; Count <= 17; Count++)
    {
        int Length = snprintf(Text, sizeof(Text), "{\"name\": \"t\", \"elements\": {\"button\": {\"variants\": {");
        for (int Index = 0; Index < Count; Index++)
        {
            Length += snprintf(Text + Length, sizeof(Text) - (size_t)Length, "%s\"v%d\": {}", (Index > 0) ? ", " : "", Index);
        }
        snprintf(Text + Length, sizeof(Text) - (size_t)Length, "}}}}");
        if (Count <= 16)
        {
            TEST_CHECK_SUCCESS(run, BuildTheme(&Fixture, Text));
        }
        else
        {
            TEST_CHECK_ERROR(run, BuildTheme(&Fixture, Text), ErrorCode_InvalidAssetDefinition);
        }
    }

    // 64 different textures read, 65 do not; one name used twice counts once.
    for (int Count = 64; Count <= 65; Count++)
    {
        int Length = snprintf(Text, sizeof(Text), "{\"name\": \"t\", \"elements\": {\"button\": {\"background\": {\"kind\": \"none\"}, \"variants\": {");
        for (int Index = 0; (Index < 16) && (Index < Count); Index++)
        {
            Length += snprintf(Text + Length, sizeof(Text) - (size_t)Length,
                "%s\"v%d\": {\"background\": {\"kind\": \"nine_slice\", \"margins\": 1, \"texture\": \"x%d\"}}", (Index > 0) ? ", " : "", Index, Index);
        }
        Length += snprintf(Text + Length, sizeof(Text) - (size_t)Length, "}}");
        // The other textures in the slider's parts' variants: three parts a variant.
        Length += snprintf(Text + Length, sizeof(Text) - (size_t)Length, ", \"slider\": {\"variants\": {");
        int Name = 16;
        for (int Variant = 0; (Variant < 16) && (Name < Count); Variant++)
        {
            Length += snprintf(Text + Length, sizeof(Text) - (size_t)Length, "%s\"v%d\": {", (Variant > 0) ? ", " : "", Variant);
            const char* Parts[] = { "track", "fill", "thumb" };
            for (int Part = 0; (Part < 3) && (Name < Count); Part++)
            {
                Length += snprintf(Text + Length, sizeof(Text) - (size_t)Length,
                    "%s\"%s\": {\"kind\": \"nine_slice\", \"margins\": 1, \"texture\": \"x%d\"}", (Part > 0) ? ", " : "", Parts[Part], Name);
                Name++;
            }
            Length += snprintf(Text + Length, sizeof(Text) - (size_t)Length, "}");
        }
        Length += snprintf(Text + Length, sizeof(Text) - (size_t)Length, "}}");
        // One more, and the first name again, which counts as nothing new.
        if (Name < Count)
        {
            Length += snprintf(Text + Length, sizeof(Text) - (size_t)Length, ", \"toggle\": {\"box\": {\"kind\": \"nine_slice\", \"margins\": 1, \"texture\": \"x%d\"}}", Name);
        }
        else
        {
            Length += snprintf(Text + Length, sizeof(Text) - (size_t)Length, ", \"toggle\": {\"box\": {\"kind\": \"nine_slice\", \"margins\": 1, \"texture\": \"x0\"}}");
        }
        snprintf(Text + Length, sizeof(Text) - (size_t)Length, "}}");
        if (Count <= 64)
        {
            TEST_CHECK_SUCCESS(run, BuildTheme(&Fixture, Text));
        }
        else
        {
            const char* Parts[] = { "at most 64", NULL };
            TEST_CHECK(run, TestText_CheckErrorMessage(run, BuildTheme(&Fixture, Text), ErrorCode_InvalidAssetDefinition, Parts));
        }
    }

    TEST_CHECK_SUCCESS(run, BuildTheme(&Fixture, "{\"name\": \"t\", \"text_size\": 256, \"transition_time\": 5}"));
    TEST_CHECK_ERROR(run, BuildTheme(&Fixture, "{\"name\": \"t\", \"text_size\": 1e30}"), ErrorCode_InvalidAssetDefinition);
    TEST_CHECK_ERROR(run, BuildTheme(&Fixture, "{\"name\": \"t\", \"elements\": {\"button\": {\"padding\": [1e308, 1]}}}"), ErrorCode_InvalidAssetDefinition);
    TEST_CHECK_ERROR(run, BuildTheme(&Fixture, "not json"), ErrorCode_InvalidJSON);
    TEST_CHECK_ERROR(run, BuildTheme(&Fixture, "[]"), ErrorCode_InvalidAssetDefinition);
    TestAssetFixture_Deconstruct(run, &Fixture);
}


/* 64 palette colours and 16 custom values read, one more does not; a variant counts what it inherits from its kind. */
static void PaletteAndCustomCountsHaveTheirLimits(TestRun* run)
{
    TestAssetFixture Fixture;
    if (!TestAssetFixture_Construct(run, &Fixture))
    {
        return;
    }
    char Text[THEME_TEXT_CAPACITY * 8U];
    for (int Count = 64; Count <= 65; Count++)
    {
        int Length = snprintf(Text, sizeof(Text), "{\"name\": \"t\", \"palette\": {");
        for (int Index = 0; Index < Count; Index++)
        {
            Length += snprintf(Text + Length, sizeof(Text) - (size_t)Length, "%s\"c%d\": \"#0000%02x\"", (Index > 0) ? ", " : "", Index, Index);
        }
        snprintf(Text + Length, sizeof(Text) - (size_t)Length, "}, \"elements\": {\"label\": {\"text\": {\"normal\": \"@c%d\"}}}}", Count - 1);
        if (Count <= 64)
        {
            TEST_CHECK_SUCCESS(run, BuildTheme(&Fixture, Text));
        }
        else
        {
            const char* Parts[] = { "palette: a palette may name at most 64 colours", NULL };
            TEST_CHECK(run, TestText_CheckErrorMessage(run, BuildTheme(&Fixture, Text), ErrorCode_InvalidAssetDefinition, Parts));
        }
    }

    for (int Count = 16; Count <= 17; Count++)
    {
        int Length = snprintf(Text, sizeof(Text), "{\"name\": \"t\", \"elements\": {\"toggle\": {\"custom\": {");
        for (int Index = 0; Index < Count; Index++)
        {
            Length += snprintf(Text + Length, sizeof(Text) - (size_t)Length, "%s\"k%d\": %d", (Index > 0) ? ", " : "", Index, Index);
        }
        snprintf(Text + Length, sizeof(Text) - (size_t)Length, "}}}}");
        if (Count <= 16)
        {
            TEST_CHECK_SUCCESS(run, BuildTheme(&Fixture, Text));
        }
        else
        {
            const char* Parts[] = { "elements.toggle.custom: a style may have at most 16 custom values", NULL };
            TEST_CHECK(run, TestText_CheckErrorMessage(run, BuildTheme(&Fixture, Text), ErrorCode_InvalidAssetDefinition, Parts));
        }
    }

    // The kind's 10 and the variant's 6 more fill the 16; a 7th does not fit, whatever the variant names again.
    static const struct { int KindCount; int OverrideCount; int AddCount; bool IsRead; } Merges[] =
    {
        { 10, 0, 6, true }, { 10, 0, 7, false }, { 10, 10, 6, true }, { 10, 5, 7, false }, { 16, 16, 0, true },
        { 16, 15, 1, false }, { 0, 0, 16, true }, { 1, 0, 16, false },
    };
    for (size_t Index = 0U; Index < (sizeof(Merges) / sizeof(Merges[0])); Index++)
    {
        WriteMergedCustomTheme(Text, sizeof(Text), Merges[Index].KindCount, Merges[Index].OverrideCount, Merges[Index].AddCount);
        if (Merges[Index].IsRead)
        {
            TEST_CHECK_SUCCESS(run, BuildTheme(&Fixture, Text));
            continue;
        }
        const char* Parts[] = { "elements.button.variants.v.custom.n", "a variant's own and its kind's together", NULL };
        if (!TestText_CheckErrorMessage(run, BuildTheme(&Fixture, Text), ErrorCode_InvalidAssetDefinition, Parts))
        {
            printf("    in merge %zu\n", Index);
        }
    }

    // A number past a double's range never reaches the theme: the JSON reader refuses the document.
    TEST_CHECK_ERROR(run, BuildTheme(&Fixture, "{\"name\": \"t\", \"elements\": {\"button\": {\"custom\": {\"x\": 1e400}}}}"), ErrorCode_InvalidJSON);
    TEST_CHECK_ERROR(run, BuildTheme(&Fixture, "{\"name\": \"t\", \"elements\": {\"button\": {\"custom\": {\"x\": [0, -1e400]}}}}"), ErrorCode_InvalidJSON);
    TestAssetFixture_Deconstruct(run, &Fixture);
}


// Tests: palettes, custom values, looks and hover text.
/* Every colour can name the palette, however the file orders its keys; a variant's references resolve on top of its kind's
 * values, and a literal colour mixes with them. */
static void PaletteColorsResolveEverywhere(TestRun* run)
{
    TestAssetFixture Fixture;
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (!TestAssetFixture_Construct(run, &Fixture))
    {
        return;
    }
    const UITheme* Theme = LoadTheme(run, &Fixture, PALETTE_THEME, "painted", &User);
    if (TEST_CHECK(run, Theme != NULL))
    {
        const UIThemeStyle* Base = UITheme_FindStyle(Theme, UIElementKind_Button, NULL);
        const UIBoxStyle* Box = UIThemeStyle_GetBox(Base, UIButtonPart_Background);
        if (TEST_CHECK(run, Box != NULL))
        {
            TEST_CHECK(run, IsColor(UIBoxStyle_Resolve(Box, UIElementState_Normal).Fill, Rgb(0x10, 0x20, 0x30)));
            TEST_CHECK(run, IsColor(UIBoxStyle_Resolve(Box, UIElementState_Hovered).Fill, Rgb(0xaa, 0xbb, 0xcc)));
            TEST_CHECK(run, IsColor(UIBoxStyle_Resolve(Box, UIElementState_Normal).Outline, Rgb(0x40, 0x50, 0x60)));
        }
        // Every plain colour form is a palette colour: a hex string with its alpha, an array, a compound.
        const UIStateColors* Text = UIThemeStyle_GetColors(Base, UIButtonPart_Text);
        if (TEST_CHECK(run, Text != NULL))
        {
            TEST_CHECK(run, IsColor(UIStateColors_Resolve(Text, UIElementState_Normal, BLANK), Rgb(1, 2, 3)));
            TEST_CHECK(run, IsColor(UIStateColors_Resolve(Text, UIElementState_Disabled, BLANK), (Color){ 0x11, 0x22, 0x33, 0x44 }));
        }
        TEST_CHECK(run, IsColor(UIThemeStyle_FindCustomColor(Base, u8"glow", BLANK), (Color){ 7, 8, 9, 10 }));

        const UIThemeStyle* Loud = UITheme_FindStyle(Theme, UIElementKind_Button, u8"loud");
        const UIBoxStyle* LoudBox = UIThemeStyle_GetBox(Loud, UIButtonPart_Background);
        if (TEST_CHECK(run, (Loud != Base) && (LoudBox != NULL)))
        {
            TEST_CHECK(run, IsColor(UIBoxStyle_Resolve(LoudBox, UIElementState_Normal).Fill, Rgb(0x40, 0x50, 0x60)));
            TEST_CHECK(run, IsColor(UIBoxStyle_Resolve(LoudBox, UIElementState_Hovered).Fill, Rgb(0xaa, 0xbb, 0xcc)));
            TEST_CHECK(run, IsColor(UIBoxStyle_Resolve(LoudBox, UIElementState_Normal).Outline, Rgb(0x40, 0x50, 0x60)));
        }
        TEST_CHECK(run, IsColor(UIThemeStyle_FindCustomColor(Loud, u8"glow", BLANK), Rgb(1, 2, 3)));
        TEST_CHECK(run, IsColor(UIThemeStyle_FindCustomColor(Base, u8"glow", BLANK), (Color){ 7, 8, 9, 10 })); // the kind's kept
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

static void CustomValuesOfEveryKindAreRead(TestRun* run)
{
    TestAssetFixture Fixture;
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (!TestAssetFixture_Construct(run, &Fixture))
    {
        return;
    }
    TEST_CHECK(run, TestAssets_AddTexture(run, &Fixture, "art", 7U, NULL));
    const UITheme* Theme = LoadTheme(run, &Fixture, CUSTOM_THEME, "custom", &User);
    if (TEST_CHECK(run, Theme != NULL))
    {
        const UIThemeStyle* Style = UITheme_FindStyle(Theme, UIElementKind_Slider, NULL);
        TEST_CHECK(run, UIThemeStyle_FindCustomNumber(Style, u8"radius", 0.0f) == 4.5f);
        TEST_CHECK(run, UIThemeStyle_FindCustomNumber(Style, u8"count", 0.0f) == 3.0f);
        TEST_CHECK(run, UIThemeStyle_FindCustomNumber(Style, u8"lowest", 0.0f) == -UI_THEME_MAX_CUSTOM_NUMBER);
        TEST_CHECK(run, UIThemeStyle_FindCustomNumber(Style, u8"highest", 0.0f) == UI_THEME_MAX_CUSTOM_NUMBER);
        Vector2 Offset = UIThemeStyle_FindCustomVector(Style, u8"offset", (Vector2){ 0.0f, 0.0f });
        TEST_CHECK(run, (Offset.x == 1.0f) && (Offset.y == -2.5f));
        TEST_CHECK(run, IsColor(UIThemeStyle_FindCustomColor(Style, u8"tint", BLANK), (Color){ 255, 0, 0, 128 }));
        TEST_CHECK(run, IsColor(UIThemeStyle_FindCustomColor(Style, u8"gold", BLANK), Rgb(0xff, 0xd2, 0x7a)));
        const UIBoxStyle* Frame = UIThemeStyle_FindCustomBox(Style, u8"frame");
        if (TEST_CHECK(run, Frame != NULL))
        {
            TEST_CHECK(run, (Frame->Kind == UIBoxKind_Flat) && Frame->HasOutline && (Frame->OutlineThickness == 2.0f));
            TEST_CHECK(run, IsColor(UIBoxStyle_Resolve(Frame, UIElementState_Pressed).Fill, Rgb(0xff, 0xd2, 0x7a)));
        }
        const UIBoxStyle* Art = UIThemeStyle_FindCustomBox(Style, u8"art");
        if (TEST_CHECK(run, Art != NULL))
        {
            TEST_CHECK(run, (Art->Kind == UIBoxKind_NineSlice) && (Art->Margins.Left == 2.0f) && (Art->PixelScale == 1.0f));
            TEST_CHECK(run, Art->Texture != NULL); // loaded with the theme
        }
        TEST_CHECK(run, UIThemeStyle_FindCustomBox(UITheme_FindStyle(Theme, UIElementKind_Slider, u8"plain"), u8"art") != Art);
        TEST_CHECK(run, UIThemeStyle_FindCustomNumber(UITheme_FindStyle(Theme, UIElementKind_Slider, u8"plain"), u8"radius", 0.0f) == 4.5f);

        // A key that is absent, or holds another kind of value, answers the default.
        TEST_CHECK(run, UIThemeStyle_FindCustomNumber(Style, u8"missing", 7.0f) == 7.0f);
        TEST_CHECK(run, UIThemeStyle_FindCustomNumber(Style, u8"offset", 7.0f) == 7.0f);
        TEST_CHECK(run, UIThemeStyle_FindCustomNumber(Style, u8"tint", 7.0f) == 7.0f);
        Vector2 NotAVector = UIThemeStyle_FindCustomVector(Style, u8"radius", (Vector2){ 3.0f, 4.0f });
        TEST_CHECK(run, (NotAVector.x == 3.0f) && (NotAVector.y == 4.0f));
        TEST_CHECK(run, IsColor(UIThemeStyle_FindCustomColor(Style, u8"frame", RED), RED));
        TEST_CHECK(run, IsColor(UIThemeStyle_FindCustomColor(Style, u8"missing", RED), RED));
        TEST_CHECK(run, UIThemeStyle_FindCustomBox(Style, u8"tint") == NULL);
        TEST_CHECK(run, UIThemeStyle_FindCustomBox(Style, u8"missing") == NULL);
        TEST_CHECK(run, UIThemeStyle_FindCustomNumber(Style, u8"radiusradiusradiusradiusradiusradius", 7.0f) == 7.0f);
        TEST_CHECK(run, UIThemeStyle_FindCustomNumber(Style, u8"Radius", 7.0f) == 7.0f);

        // A style without a custom block, a NULL style and a NULL key answer the default.
        const UIThemeStyle* Button = UITheme_FindStyle(Theme, UIElementKind_Button, NULL);
        TEST_CHECK(run, (Button != NULL) && (UIThemeStyle_FindCustomNumber(Button, u8"radius", 7.0f) == 7.0f));
        TEST_CHECK(run, UIThemeStyle_FindCustomNumber(Style, NULL, 7.0f) == 7.0f);
        TEST_CHECK(run, UIThemeStyle_FindCustomBox(Style, NULL) == NULL);
    }
    TEST_CHECK(run, UIThemeStyle_FindCustomNumber(NULL, u8"radius", 7.0f) == 7.0f);
    Vector2 Fallback = UIThemeStyle_FindCustomVector(NULL, u8"offset", (Vector2){ 3.0f, 4.0f });
    TEST_CHECK(run, (Fallback.x == 3.0f) && (Fallback.y == 4.0f));
    TEST_CHECK(run, IsColor(UIThemeStyle_FindCustomColor(NULL, u8"tint", RED), RED));
    TEST_CHECK(run, UIThemeStyle_FindCustomBox(NULL, u8"frame") == NULL);
    TestAssetFixture_Deconstruct(run, &Fixture);
}

/* A variant starts with its kind's custom values: its own replace the ones of their key, whatever their kind, or add to them. */
static void AVariantMergesCustomValuesByKey(TestRun* run)
{
    TestAssetFixture Fixture;
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (!TestAssetFixture_Construct(run, &Fixture))
    {
        return;
    }
    const UITheme* Theme = LoadTheme(run, &Fixture, "{\"name\": \"t\", \"elements\": {\"panel\": {"
        "\"custom\": {\"a\": 1, \"b\": \"#010203\", \"c\": [1, 2]},"
        "\"variants\": {\"v\": {\"custom\": {\"b\": 7, \"d\": {\"kind\": \"none\"}}}, \"w\": {\"padding\": [1, 1]}}}}}", "t", &User);
    if (TEST_CHECK(run, Theme != NULL))
    {
        const UIThemeStyle* Base = UITheme_FindStyle(Theme, UIElementKind_Panel, NULL);
        const UIThemeStyle* V = UITheme_FindStyle(Theme, UIElementKind_Panel, u8"v");
        const UIThemeStyle* W = UITheme_FindStyle(Theme, UIElementKind_Panel, u8"w");
        TEST_CHECK(run, (V != Base) && (W != Base));

        TEST_CHECK(run, UIThemeStyle_FindCustomNumber(V, u8"a", 0.0f) == 1.0f);                       // inherited
        TEST_CHECK(run, UIThemeStyle_FindCustomNumber(V, u8"b", 0.0f) == 7.0f);                       // replaced by a number
        TEST_CHECK(run, IsColor(UIThemeStyle_FindCustomColor(V, u8"b", RED), RED));                   // the colour is gone
        TEST_CHECK(run, UIThemeStyle_FindCustomVector(V, u8"c", (Vector2){ 0.0f, 0.0f }).y == 2.0f); // inherited
        const UIBoxStyle* Added = UIThemeStyle_FindCustomBox(V, u8"d");
        TEST_CHECK(run, (Added != NULL) && (Added->Kind == UIBoxKind_None));                          // added

        // The kind's own values did not change with it, and a variant without a block has them all.
        TEST_CHECK(run, IsColor(UIThemeStyle_FindCustomColor(Base, u8"b", RED), Rgb(1, 2, 3)));
        TEST_CHECK(run, UIThemeStyle_FindCustomBox(Base, u8"d") == NULL);
        TEST_CHECK(run, UIThemeStyle_FindCustomNumber(W, u8"a", 0.0f) == 1.0f);
        TEST_CHECK(run, IsColor(UIThemeStyle_FindCustomColor(W, u8"b", RED), Rgb(1, 2, 3)));
        TEST_CHECK(run, UIThemeStyle_FindCustomVector(W, u8"c", (Vector2){ 0.0f, 0.0f }).x == 1.0f);
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

/* A custom nine-slice's texture loads with the theme like a part's; a variant's copy of it too, and again after the theme
 * is unloaded and loaded anew. Art that cannot be used leaves only that box plain. */
static void ACustomNineSliceGetsItsTexture(TestRun* run)
{
    TestAssetFixture Fixture;
    TestAssetCounters Counters = { 0 };
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (!TestAssetFixture_Construct(run, &Fixture))
    {
        return;
    }
    TEST_CHECK(run, TestAssets_AddTexture(run, &Fixture, "art", 7U, &Counters)); // 16 x 16
    TEST_CHECK(run, TestAssets_AddTexture(run, &Fixture, "other", 8U, NULL));
    const char* Json = "{\"name\": \"t\", \"elements\": {\"slider\": {\"custom\": {"
        "\"art\": {\"kind\": \"nine_slice\", \"texture\": \"art\", \"margins\": 2, \"sources\": {\"hovered\": {\"x\": 8, \"y\": 8, \"width\": 8, \"height\": 8}}},"
        "\"missing\": {\"kind\": \"nine_slice\", \"texture\": \"nobody\", \"margins\": 2},"
        "\"wide\": {\"kind\": \"nine_slice\", \"texture\": \"art\", \"margins\": [9, 0, 9, 0]}},"
        "\"variants\": {\"plain\": {}, \"own\": {\"custom\": {\"art\": {\"kind\": \"nine_slice\", \"texture\": \"other\", \"margins\": 1}}}}}}}";
    const UITheme* Theme = LoadTheme(run, &Fixture, Json, "t", &User);
    for (int Round = 0; (Round < 2) && TEST_CHECK(run, Theme != NULL); Round++)
    {
        const UIThemeStyle* Base = UITheme_FindStyle(Theme, UIElementKind_Slider, NULL);
        const UIBoxStyle* Art = UIThemeStyle_FindCustomBox(Base, u8"art");
        const GameTexture* ArtTexture = (Art != NULL) ? Art->Texture : NULL;
        if (TEST_CHECK(run, ArtTexture != NULL))
        {
            UIBoxPaint Normal = UIBoxStyle_Resolve(Art, UIElementState_Normal);
            TEST_CHECK(run, (Normal.Source.width == 16.0f) && (Normal.Source.height == 16.0f)); // the whole texture
            TEST_CHECK(run, UIBoxStyle_Resolve(Art, UIElementState_Hovered).Source.x == 8.0f);
        }
        const UIBoxStyle* Inherited = UIThemeStyle_FindCustomBox(UITheme_FindStyle(Theme, UIElementKind_Slider, u8"plain"), u8"art");
        TEST_CHECK(run, (Inherited != NULL) && (Inherited != Art) && (Inherited->Texture == ArtTexture));
        const UIBoxStyle* Own = UIThemeStyle_FindCustomBox(UITheme_FindStyle(Theme, UIElementKind_Slider, u8"own"), u8"art");
        TEST_CHECK(run, (Own != NULL) && (Own->Texture != NULL) && (Own->Texture != ArtTexture));
        const UIBoxStyle* Missing = UIThemeStyle_FindCustomBox(Base, u8"missing");
        const UIBoxStyle* Wide = UIThemeStyle_FindCustomBox(Base, u8"wide");
        TEST_CHECK(run, (Missing != NULL) && (Missing->Texture == NULL)); // a texture that does not load
        TEST_CHECK(run, (Wide != NULL) && (Wide->Texture == NULL));       // margins wider than the art
        TEST_CHECK(run, Counters.LoadCount == (size_t)(Round + 1));

        // Unloaded with its user and loaded anew from the same definition.
        TEST_CHECK_SUCCESS(run, AssetManager_RetireUser(Fixture.Manager, User));
        TEST_CHECK(run, Counters.UnloadCount == (size_t)(Round + 1));
        UITheme* Again = NULL;
        Theme = NULL;
        if (TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(Fixture.Manager, &User))
            && TEST_CHECK_SUCCESS(run, AssetManager_LoadUITheme(Fixture.Manager, (const unsigned char*)u8"t", User, &Again)))
        {
            Theme = Again;
        }
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

static void AKindNamesItsLook(TestRun* run)
{
    TestAssetFixture Fixture;
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (!TestAssetFixture_Construct(run, &Fixture))
    {
        return;
    }
    const UITheme* Theme = LoadTheme(run, &Fixture, "{\"name\": \"t\", \"elements\": {\"button\": {\"look\": \"round_button\", \"variants\": {\"v\": {}}},"
        "\"slider\": {\"look\": \"mod:knob\"}, \"tooltip\": {\"look\": \"tbp:speech_bubble\"}, \"label\": {}}}", "t", &User);
    if (TEST_CHECK(run, Theme != NULL))
    {
        const unsigned char* Button = UITheme_GetLookName(Theme, UIElementKind_Button);
        TEST_CHECK(run, (Button != NULL) && (strcmp((const char*)Button, "tbp:round_button") == 0)); // the default namespace
        const unsigned char* Slider = UITheme_GetLookName(Theme, UIElementKind_Slider);
        TEST_CHECK(run, (Slider != NULL) && (strcmp((const char*)Slider, "mod:knob") == 0));
        const unsigned char* Tooltip = UITheme_GetLookName(Theme, UIElementKind_Tooltip);
        TEST_CHECK(run, (Tooltip != NULL) && (strcmp((const char*)Tooltip, "tbp:speech_bubble") == 0));
        TEST_CHECK(run, UITheme_GetLookName(Theme, UIElementKind_Label) == NULL); // a kind that names none
        TEST_CHECK(run, UITheme_GetLookName(Theme, UIElementKind_Panel) == NULL); // a kind the theme leaves out
        TEST_CHECK(run, UITheme_GetLookName(Theme, (UIElementKind)99) == NULL);
        TEST_CHECK(run, UITheme_GetLookName(Theme, (UIElementKind)-1) == NULL);
        TEST_CHECK(run, UITheme_FindStyle(Theme, UIElementKind_Button, u8"v") != UITheme_FindStyle(Theme, UIElementKind_Button, NULL));
    }
    TEST_CHECK(run, UITheme_GetLookName(NULL, UIElementKind_Button) == NULL);
    TestAssetFixture_Deconstruct(run, &Fixture);
}

static void TheTooltipDelayHasItsBoundsAndDefault(TestRun* run)
{
    TEST_CHECK(run, UITheme_GetTooltipDelaySeconds(NULL) == UI_THEME_DEFAULT_TOOLTIP_DELAY_SECONDS);
    TestAssetFixture Fixture;
    if (!TestAssetFixture_Construct(run, &Fixture))
    {
        return;
    }
    static const struct { const char* Json; const char* Name; float Seconds; } Themes[] =
    {
        { "{\"name\": \"a\"}", "a", UI_THEME_DEFAULT_TOOLTIP_DELAY_SECONDS },
        { "{\"name\": \"b\", \"tooltip_delay\": 0}", "b", 0.0f },
        { "{\"name\": \"c\", \"tooltip_delay\": 10}", "c", UI_THEME_MAX_TOOLTIP_DELAY_SECONDS },
        { "{\"name\": \"d\", \"tooltip_delay\": 2.25}", "d", 2.25f },
    };
    for (size_t Index = 0U; Index < (sizeof(Themes) / sizeof(Themes[0])); Index++)
    {
        AssetUserID User = ASSET_USER_ID_INVALID;
        const UITheme* Theme = LoadTheme(run, &Fixture, Themes[Index].Json, Themes[Index].Name, &User);
        TEST_CHECK(run, (Theme != NULL) && (UITheme_GetTooltipDelaySeconds(Theme) == Themes[Index].Seconds));
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

/* Hover text is a kind like the others: its parts, their defaults, variants and custom values. */
static void TheTooltipKindHasItsParts(TestRun* run)
{
    TEST_CHECK(run, strcmp((const char*)UIElementKind_GetName(UIElementKind_Tooltip), "tooltip") == 0);
    TEST_CHECK(run, UIElementKind_GetPartCount(UIElementKind_Tooltip) == (size_t)UITooltipPart_Count);
    static const struct { int32_t Part; const char* Key; UIThemePartKind Kind; } Parts[] =
    {
        { UITooltipPart_Padding, "padding", UIThemePartKind_Vector }, { UITooltipPart_Offset, "offset", UIThemePartKind_Vector },
        { UITooltipPart_MaxWidth, "max_width", UIThemePartKind_Number }, { UITooltipPart_Background, "background", UIThemePartKind_Box },
        { UITooltipPart_Text, "text", UIThemePartKind_Colors },
    };
    for (size_t Index = 0U; Index < (sizeof(Parts) / sizeof(Parts[0])); Index++)
    {
        const UIThemePartDescriptor* Descriptor = UIElementKind_GetPart(UIElementKind_Tooltip, Parts[Index].Part);
        TEST_CHECK(run, (Descriptor != NULL) && (strcmp((const char*)Descriptor->Key, Parts[Index].Key) == 0)
            && (Descriptor->Kind == Parts[Index].Kind));
    }
    // The offset points either way; every other metric of every kind counts from 0.
    for (int32_t Kind = 0; Kind < (int32_t)UIElementKind_Count; Kind++)
    {
        for (size_t Part = 0U; Part < UIElementKind_GetPartCount((UIElementKind)Kind); Part++)
        {
            const UIThemePartDescriptor* Descriptor = UIElementKind_GetPart((UIElementKind)Kind, (int32_t)Part);
            bool IsMetric = (Descriptor->Kind == UIThemePartKind_Vector) || (Descriptor->Kind == UIThemePartKind_Number);
            bool IsOffset = (Kind == (int32_t)UIElementKind_Tooltip) && (Part == (size_t)UITooltipPart_Offset);
            TEST_CHECK(run, !IsMetric || (Descriptor->Maximum == UI_THEME_MAX_METRIC));
            TEST_CHECK(run, !IsMetric || (Descriptor->Minimum == (IsOffset ? -UI_THEME_MAX_METRIC : 0.0f)));
        }
    }
    Vector2 Padding = UIThemeStyle_GetVector(NULL, UIElementKind_Tooltip, UITooltipPart_Padding);
    Vector2 Offset = UIThemeStyle_GetVector(NULL, UIElementKind_Tooltip, UITooltipPart_Offset);
    TEST_CHECK(run, (Padding.x == 6.0f) && (Padding.y == 3.0f) && (Offset.x == 12.0f) && (Offset.y == 18.0f));
    TEST_CHECK(run, UIThemeStyle_GetNumber(NULL, UIElementKind_Tooltip, UITooltipPart_MaxWidth) == 320.0f);

    TestAssetFixture Fixture;
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (!TestAssetFixture_Construct(run, &Fixture))
    {
        return;
    }
    const UITheme* Theme = LoadTheme(run, &Fixture, "{\"name\": \"t\", \"palette\": {\"ink\": \"#202020\"}, \"elements\": {\"tooltip\": {"
        "\"padding\": [4, 2], \"max_width\": 200, \"background\": {\"kind\": \"flat\", \"colors\": {\"normal\": \"#fafafa\"}},"
        "\"text\": {\"normal\": \"@ink\"}, \"custom\": {\"tail\": 6},"
        "\"variants\": {\"warning\": {\"background\": {\"colors\": {\"normal\": \"#ffe08a\"}}, \"offset\": [0, 24]}}}}}", "t", &User);
    if (TEST_CHECK(run, Theme != NULL))
    {
        const UIThemeStyle* Style = UITheme_FindStyle(Theme, UIElementKind_Tooltip, NULL);
        Vector2 ReadPadding = UIThemeStyle_GetVector(Style, UIElementKind_Tooltip, UITooltipPart_Padding);
        TEST_CHECK(run, (ReadPadding.x == 4.0f) && (ReadPadding.y == 2.0f));
        TEST_CHECK(run, UIThemeStyle_GetVector(Style, UIElementKind_Tooltip, UITooltipPart_Offset).y == 18.0f); // left out
        TEST_CHECK(run, UIThemeStyle_GetNumber(Style, UIElementKind_Tooltip, UITooltipPart_MaxWidth) == 200.0f);
        TEST_CHECK(run, IsColor(PartColor(Theme, UIElementKind_Tooltip, NULL, UITooltipPart_Background, UIElementState_Normal), Rgb(0xfa, 0xfa, 0xfa)));
        TEST_CHECK(run, IsColor(PartColor(Theme, UIElementKind_Tooltip, NULL, UITooltipPart_Text, UIElementState_Normal), Rgb(0x20, 0x20, 0x20)));
        TEST_CHECK(run, UIThemeStyle_FindCustomNumber(Style, u8"tail", 0.0f) == 6.0f);

        const UIThemeStyle* Warning = UITheme_FindStyle(Theme, UIElementKind_Tooltip, u8"warning");
        TEST_CHECK(run, IsColor(PartColor(Theme, UIElementKind_Tooltip, "warning", UITooltipPart_Background, UIElementState_Normal), Rgb(0xff, 0xe0, 0x8a)));
        TEST_CHECK(run, UIThemeStyle_GetVector(Warning, UIElementKind_Tooltip, UITooltipPart_Offset).y == 24.0f);
        TEST_CHECK(run, UIThemeStyle_GetNumber(Warning, UIElementKind_Tooltip, UITooltipPart_MaxWidth) == 200.0f);
        TEST_CHECK(run, UIThemeStyle_FindCustomNumber(Warning, u8"tail", 0.0f) == 6.0f);
    }
    // An offset left of and above the pointer, as far as a metric goes.
    AssetUserID AboveUser = ASSET_USER_ID_INVALID;
    const UITheme* Above = LoadTheme(run, &Fixture, "{\"name\": \"above\", \"elements\": {\"tooltip\": {\"offset\": [-4096, -30.5],"
        "\"variants\": {\"right\": {\"offset\": [4096, 0]}}}}}", "above", &AboveUser);
    if (TEST_CHECK(run, Above != NULL))
    {
        Vector2 Read = UIThemeStyle_GetVector(UITheme_FindStyle(Above, UIElementKind_Tooltip, NULL), UIElementKind_Tooltip, UITooltipPart_Offset);
        TEST_CHECK(run, (Read.x == -4096.0f) && (Read.y == -30.5f));
        Read = UIThemeStyle_GetVector(UITheme_FindStyle(Above, UIElementKind_Tooltip, u8"right"), UIElementKind_Tooltip, UITooltipPart_Offset);
        TEST_CHECK(run, (Read.x == 4096.0f) && (Read.y == 0.0f));
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}


// Tests: the shipped themes.
/* Each shipped theme loads with every palette reference resolved, its looks show the colours checked here, and it has hover
 * text of its own. The art and the font are built in code. */
static void TheShippedThemesLoadAndResolve(TestRun* run)
{
    static const char* const Files[] = { "test_flat.json", "test_nine_slice.json", "test_light.json" };
    static const char* const Names[] = { "tbp:test_flat", "tbp:test_nine_slice", "tbp:test_light" };
    const UITheme* Themes[3] = { NULL, NULL, NULL };
    TestAssetFixture Fixture;
    if (!TestAssetFixture_Construct(run, &Fixture))
    {
        return;
    }
    TEST_CHECK(run, AddShippedThemeDependencies(run, &Fixture));
    for (size_t Index = 0U; Index < (sizeof(Files) / sizeof(Files[0])); Index++)
    {
        GenericBuffer Text;
        GenericBuffer_AllocateVariable(&Text, 0U, sizeof(unsigned char));
        AssetUserID User = ASSET_USER_ID_INVALID;
        if (ReadShippedTheme(run, Files[Index], &Text))
        {
            Themes[Index] = LoadTheme(run, &Fixture, (const char*)Text._data, Names[Index], &User);
        }
        Memory_Free(Text._data);
        if (TEST_CHECK(run, Themes[Index] != NULL))
        {
            TEST_CHECK(run, strcmp((const char*)UITheme_GetName(Themes[Index]), Names[Index]) == 0);
            TEST_CHECK(run, strcmp((const char*)UITheme_GetFontName(Themes[Index]), "tbp:ui_small") == 0);
            TEST_CHECK(run, UITheme_GetTooltipDelaySeconds(Themes[Index]) == 0.5f);
            TEST_CHECK(run, UIThemeStyle_GetBox(UITheme_FindStyle(Themes[Index], UIElementKind_Tooltip, NULL), UITooltipPart_Background) != NULL);
            TEST_CHECK(run, UIThemeStyle_GetColors(UITheme_FindStyle(Themes[Index], UIElementKind_Tooltip, NULL), UITooltipPart_Text) != NULL);
        }
    }

    const UITheme* Flat = Themes[0];
    if (Flat != NULL)
    {
        TEST_CHECK(run, IsColor(PartColor(Flat, UIElementKind_Button, NULL, UIButtonPart_Background, UIElementState_Normal), Rgb(0x3a, 0x3d, 0x44)));
        TEST_CHECK(run, IsColor(PartColor(Flat, UIElementKind_Button, NULL, UIButtonPart_Background, UIElementState_Disabled), Rgb(0x2c, 0x2e, 0x33)));
        TEST_CHECK(run, IsColor(UIBoxStyle_Resolve(UIThemeStyle_GetBox(UITheme_FindStyle(Flat, UIElementKind_Button, NULL), UIButtonPart_Background),
            UIElementState_Focused).Outline, Rgb(0x9e, 0xc1, 0xff)));
        TEST_CHECK(run, IsColor(PartColor(Flat, UIElementKind_Button, "danger", UIButtonPart_Background, UIElementState_Hovered), Rgb(0x92, 0x34, 0x34)));
        TEST_CHECK(run, IsColor(PartColor(Flat, UIElementKind_Button, "danger", UIButtonPart_Background, UIElementState_Disabled), Rgb(0x2c, 0x2e, 0x33)));
        TEST_CHECK(run, IsColor(PartColor(Flat, UIElementKind_Label, NULL, UILabelPart_Text, UIElementState_Normal), Rgb(0xe4, 0xe6, 0xea)));
        TEST_CHECK(run, IsColor(PartColor(Flat, UIElementKind_Label, "heading", UILabelPart_Text, UIElementState_Normal), Rgb(0x9e, 0xc1, 0xff)));
        TEST_CHECK(run, IsColor(PartColor(Flat, UIElementKind_Toggle, NULL, UITogglePart_Box, UIElementState_Hovered), Rgb(0x2a, 0x2c, 0x31)));
        TEST_CHECK(run, IsColor(PartColor(Flat, UIElementKind_Slider, NULL, UISliderPart_Thumb, UIElementState_Pressed), Rgb(0x9e, 0xc1, 0xff)));
        TEST_CHECK(run, IsColor(PartColor(Flat, UIElementKind_Panel, "inset", UIPanelPart_Background, UIElementState_Normal), Rgb(0x1c, 0x1e, 0x22)));
        TEST_CHECK(run, IsColor(PartColor(Flat, UIElementKind_Tooltip, NULL, UITooltipPart_Background, UIElementState_Normal), Rgb(0x3a, 0x3d, 0x44)));
        TEST_CHECK(run, IsColor(PartColor(Flat, UIElementKind_Tooltip, NULL, UITooltipPart_Text, UIElementState_Normal), Rgb(0xe8, 0xea, 0xee)));
    }
    const UITheme* NineSlice = Themes[1];
    if (NineSlice != NULL)
    {
        TEST_CHECK(run, IsColor(PartColor(NineSlice, UIElementKind_Button, NULL, UIButtonPart_Background, UIElementState_Normal), Rgb(0xb9, 0x8f, 0x4a)));
        TEST_CHECK(run, IsColor(PartColor(NineSlice, UIElementKind_Button, NULL, UIButtonPart_Background, UIElementState_Hovered), Rgb(0xd2, 0xa6, 0x5a)));
        TEST_CHECK(run, IsColor(PartColor(NineSlice, UIElementKind_Slider, NULL, UISliderPart_Fill, UIElementState_Normal), Rgb(0xd2, 0xa6, 0x5a)));
        TEST_CHECK(run, IsColor(PartColor(NineSlice, UIElementKind_Slider, NULL, UISliderPart_Track, UIElementState_Normal), Rgb(0x1e, 0x18, 0x10)));
        TEST_CHECK(run, IsColor(PartColor(NineSlice, UIElementKind_Toggle, NULL, UITogglePart_Check, UIElementState_Disabled), Rgb(0x4a, 0x44, 0x36)));
        TEST_CHECK(run, IsColor(PartColor(NineSlice, UIElementKind_Label, "heading", UILabelPart_Text, UIElementState_Normal), Rgb(0xff, 0xd2, 0x7a)));
        const UIBoxStyle* Tip = UIThemeStyle_GetBox(UITheme_FindStyle(NineSlice, UIElementKind_Tooltip, NULL), UITooltipPart_Background);
        TEST_CHECK(run, (Tip != NULL) && (Tip->Kind == UIBoxKind_NineSlice) && (Tip->Texture != NULL));
        TEST_CHECK(run, IsColor(PartColor(NineSlice, UIElementKind_Tooltip, NULL, UITooltipPart_Background, UIElementState_Normal), Rgb(0xd8, 0xc8, 0xa0)));
        TEST_CHECK(run, IsColor(PartColor(NineSlice, UIElementKind_Tooltip, NULL, UITooltipPart_Text, UIElementState_Normal), Rgb(0x1e, 0x18, 0x10)));
    }
    // The light theme's palette changed what names it, and nothing else: dark text, its own accent, the same sizes.
    const UITheme* Light = Themes[2];
    if ((Flat != NULL) && (Light != NULL))
    {
        Color Text = PartColor(Light, UIElementKind_Label, NULL, UILabelPart_Text, UIElementState_Normal);
        Color Surface = PartColor(Light, UIElementKind_Panel, NULL, UIPanelPart_Background, UIElementState_Normal);
        TEST_CHECK(run, (((int)Text.r + Text.g + Text.b) < 150) && (((int)Surface.r + Surface.g + Surface.b) > 600));
        TEST_CHECK(run, !IsColor(PartColor(Light, UIElementKind_Label, "heading", UILabelPart_Text, UIElementState_Normal),
            PartColor(Flat, UIElementKind_Label, "heading", UILabelPart_Text, UIElementState_Normal)));
        Vector2 LightPadding = UIThemeStyle_GetVector(UITheme_FindStyle(Light, UIElementKind_Button, NULL), UIElementKind_Button, UIButtonPart_Padding);
        TEST_CHECK(run, (LightPadding.x == 10.0f) && (LightPadding.y == 4.0f));
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

/* The light theme is the flat one with another palette: the files are the same once their names and palettes are taken
 * out, and the palettes name the same colours. Editing one theme without the other fails here. */
static void TheLightThemeIsTheFlatOneWithAnotherPalette(TestRun* run)
{
    TestAssetFixture Fixture;
    if (!TestAssetFixture_Construct(run, &Fixture))
    {
        return;
    }
    static const char* const Files[] = { "test_flat.json", "test_light.json" };
    JSONObjectValue Roots[2] = { { .Type = JSONValueType_None }, { .Type = JSONValueType_None } };
    GenericBuffer Texts[2];
    bool IsParsed = true;
    for (size_t Index = 0U; Index < 2U; Index++)
    {
        GenericBuffer_AllocateVariable(&Texts[Index], 0U, sizeof(unsigned char));
        GenericBuffer File;
        GenericBuffer_AllocateVariable(&File, 0U, sizeof(unsigned char));
        IsParsed = IsParsed && ReadShippedTheme(run, Files[Index], &File) && TestText_ParseJSON(run, Fixture.Pool, (const char*)File._data, &Roots[Index])
            && TEST_CHECK(run, Roots[Index].Type == JSONValueType_Compound);
        Memory_Free(File._data);
    }
    if (IsParsed)
    {
        JSONObjectValue Palettes[2];
        TEST_CHECK_SUCCESS(run, JSONCompound_GetVerified(Roots[0].Value.Compound, u8"palette", JSONValueType_Compound, &Palettes[0]));
        TEST_CHECK_SUCCESS(run, JSONCompound_GetVerified(Roots[1].Value.Compound, u8"palette", JSONValueType_Compound, &Palettes[1]));
        TEST_CHECK(run, JSONCompound_GetEntryCount(Palettes[0].Value.Compound) == JSONCompound_GetEntryCount(Palettes[1].Value.Compound));
        CollectionEnumerator* Entries = ICollection_CreateEnumerator(JSONCompound_GetEntryCollection(Palettes[0].Value.Compound));
        bool HasNext = false;
        while ((Entries != NULL) && TEST_CHECK_SUCCESS(run, CollectionEnumerator_HasNext(Entries, &HasNext)) && HasNext)
        {
            JSONCompoundEntry Entry;
            JSONObjectValue Other;
            bool IsFound = false;
            if (TEST_CHECK_SUCCESS(run, CollectionEnumerator_NextByValue(Entries, &Entry)))
            {
                TEST_CHECK_SUCCESS(run, JSONCompound_GetOptional(Palettes[1].Value.Compound, Entry.Key, &Other, &IsFound));
            }
            TEST_CHECK(run, IsFound);
        }
        CollectionEnumerator_Destroy(Entries);
        for (size_t Index = 0U; Index < 2U; Index++)
        {
            TEST_CHECK_SUCCESS(run, JSONCompound_Remove(Roots[Index].Value.Compound, u8"name"));
            TEST_CHECK_SUCCESS(run, JSONCompound_Remove(Roots[Index].Value.Compound, u8"palette"));
            TEST_CHECK_SUCCESS(run, JSON_Serialize(&Roots[Index], &Texts[Index], JSONSerializeFlags_None));
        }
        TEST_CHECK(run, (Texts[0]._count == Texts[1]._count) && (Texts[0]._count > 0U) && Memory_IsEqual(Texts[0]._data, Texts[1]._data, Texts[0]._count));
    }
    for (size_t Index = 0U; Index < 2U; Index++)
    {
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Fixture.Pool, &Roots[Index]));
        Memory_Free(Texts[Index]._data);
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}


// Tests: nine-slice art.
static void ANineSliceReadsItsArtAndItsFont(TestRun* run)
{
    TestAssetFixture Fixture;
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (TestAssetFixture_Construct(run, &Fixture))
    {
        TEST_CHECK(run, TestAssets_AddTexture(run, &Fixture, "art", 7U, NULL)); // 16 x 16
        const UITheme* Theme = LoadTheme(run, &Fixture, "{\"name\": \"t\", \"elements\": {\"button\": {\"background\": {\"kind\": \"nine_slice\","
            "\"texture\": \"art\", \"margins\": [1, 2, 3, 4], \"pixel_scale\": 2, \"colors\": {\"normal\": \"#ff0000\"},"
            "\"sources\": {\"hovered\": {\"x\": 8, \"y\": 8, \"width\": 8, \"height\": 8}}}}}}", "t", &User);
        if (TEST_CHECK(run, Theme != NULL))
        {
            const UIBoxStyle* Box = UIThemeStyle_GetBox(UITheme_FindStyle(Theme, UIElementKind_Button, NULL), UIButtonPart_Background);
            TEST_CHECK(run, (Box->Kind == UIBoxKind_NineSlice) && (Box->Texture != NULL) && (Box->PixelScale == 2.0f));
            TEST_CHECK(run, (Box->Margins.Left == 1.0f) && (Box->Margins.Top == 2.0f) && (Box->Margins.Right == 3.0f) && (Box->Margins.Bottom == 4.0f));

            // Normal's art is the whole texture, a state's its own, a state left out normal's.
            UIBoxPaint Normal = UIBoxStyle_Resolve(Box, UIElementState_Normal);
            TEST_CHECK(run, (Normal.Source.x == 0.0f) && (Normal.Source.width == 16.0f) && (Normal.Source.height == 16.0f));
            UIBoxPaint Hovered = UIBoxStyle_Resolve(Box, UIElementState_Hovered);
            TEST_CHECK(run, (Hovered.Source.x == 8.0f) && (Hovered.Source.y == 8.0f) && (Hovered.Source.width == 8.0f));
            UIBoxPaint Pressed = UIBoxStyle_Resolve(Box, UIElementState_Pressed);
            TEST_CHECK(run, (Pressed.Source.x == 0.0f) && (Pressed.Source.width == 16.0f));
            TEST_CHECK(run, IsColor(Pressed.Fill, Rgb(255, 0, 0)));
        }
        TestAssetFixture_Deconstruct(run, &Fixture);
    }
}

/* What a box leaves out: art drawn one texel to a pixel, and an outline of one pixel once it asks for an outline. */
static void ABoxsNumbersHaveTheirDefaults(TestRun* run)
{
    TestAssetFixture Fixture;
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (TestAssetFixture_Construct(run, &Fixture))
    {
        TEST_CHECK(run, TestAssets_AddTexture(run, &Fixture, "art", 7U, NULL));
        const UITheme* Theme = LoadTheme(run, &Fixture, "{\"name\": \"t\", \"elements\": {\"button\": {"
            "\"background\": {\"kind\": \"nine_slice\", \"texture\": \"art\", \"margins\": [1, 2, 3, 4]}}, "
            "\"panel\": {\"background\": {\"kind\": \"flat\", \"outline\": {\"colors\": {\"normal\": \"#ff0000\"}}}}}}", "t", &User);
        if (TEST_CHECK(run, Theme != NULL))
        {
            const UIBoxStyle* Art = UIThemeStyle_GetBox(UITheme_FindStyle(Theme, UIElementKind_Button, NULL), UIButtonPart_Background);
            TEST_CHECK(run, (Art->Kind == UIBoxKind_NineSlice) && (Art->PixelScale == 1.0f));
            const UIBoxStyle* Flat = UIThemeStyle_GetBox(UITheme_FindStyle(Theme, UIElementKind_Panel, NULL), UIPanelPart_Background);
            TEST_CHECK(run, Flat->HasOutline && (Flat->OutlineThickness == 1.0f));
        }
        TestAssetFixture_Deconstruct(run, &Fixture);
    }
}

/* A variant may turn a flat box into art by naming all of it, and art into a flat box. */
static void AVariantCanChangeABoxsKind(TestRun* run)
{
    TestAssetFixture Fixture;
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (TestAssetFixture_Construct(run, &Fixture))
    {
        TEST_CHECK(run, TestAssets_AddTexture(run, &Fixture, "art", 7U, NULL));
        const UITheme* Theme = LoadTheme(run, &Fixture, "{\"name\": \"t\", \"elements\": {\"button\": {\"background\": {\"kind\": \"flat\", \"colors\": {\"normal\": \"#010203\"}},"
            "\"variants\": {\"arty\": {\"background\": {\"kind\": \"nine_slice\", \"texture\": \"art\", \"margins\": 2}},"
            "\"flat_again\": {\"background\": {\"kind\": \"flat\"}}}}}}", "t", &User);
        if (TEST_CHECK(run, Theme != NULL))
        {
            const UIBoxStyle* Arty = UIThemeStyle_GetBox(UITheme_FindStyle(Theme, UIElementKind_Button, u8"arty"), UIButtonPart_Background);
            TEST_CHECK(run, (Arty->Kind == UIBoxKind_NineSlice) && (Arty->Texture != NULL));
            TEST_CHECK(run, IsColor(UIBoxStyle_Resolve(Arty, UIElementState_Normal).Fill, Rgb(1, 2, 3))); // kept: it did not name it
            const UIBoxStyle* Flat = UIThemeStyle_GetBox(UITheme_FindStyle(Theme, UIElementKind_Button, u8"flat_again"), UIButtonPart_Background);
            TEST_CHECK(run, (Flat->Kind == UIBoxKind_Flat) && (Flat->Texture == NULL));
        }
        TestAssetFixture_Deconstruct(run, &Fixture);
    }
}

/* What is wrong with art only shows when it loads: that part draws plain, the rest of the theme works. */
static void ArtThatCannotBeUsedLeavesOnlyItsPartPlain(TestRun* run)
{
    TestAssetFixture Fixture;
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (TestAssetFixture_Construct(run, &Fixture))
    {
        TEST_CHECK(run, TestAssets_AddTexture(run, &Fixture, "art", 7U, NULL)); // 16 x 16
        const UITheme* Theme = LoadTheme(run, &Fixture, "{\"name\": \"t\", \"font\": \"nobody\", \"elements\": {"
            "\"button\": {\"background\": {\"kind\": \"nine_slice\", \"texture\": \"missing\", \"margins\": 2}, \"min_size\": [3, 4]},"
            "\"toggle\": {\"box\": {\"kind\": \"nine_slice\", \"texture\": \"art\", \"margins\": [9, 0, 9, 0]},"
            "\"check\": {\"kind\": \"nine_slice\", \"texture\": \"art\", \"margins\": 1, \"source\": {\"x\": 10, \"y\": 0, \"width\": 8, \"height\": 8}}},"
            "\"slider\": {\"track\": {\"kind\": \"nine_slice\", \"texture\": \"art\", \"margins\": 1}, \"thumb\": {\"kind\": \"nine_slice\", \"texture\": \"art\", \"margins\": 1,"
            "\"sources\": {\"pressed\": {\"x\": 0, \"y\": 15, \"width\": 4, \"height\": 4}}}}}}", "t", &User);
        if (TEST_CHECK(run, Theme != NULL))
        {
            TEST_CHECK(run, UITheme_GetFontName(Theme) == NULL); // The built-in font: never an error.
            const UIThemeStyle* Button = UITheme_FindStyle(Theme, UIElementKind_Button, NULL);
            TEST_CHECK(run, UIThemeStyle_GetBox(Button, UIButtonPart_Background)->Texture == NULL);
            TEST_CHECK(run, UIThemeStyle_GetVector(Button, UIElementKind_Button, UIButtonPart_MinSize).x == 3.0f);

            const UIThemeStyle* Toggle = UITheme_FindStyle(Theme, UIElementKind_Toggle, NULL);
            TEST_CHECK(run, UIThemeStyle_GetBox(Toggle, UITogglePart_Box)->Texture == NULL);   // margins wider than the art
            TEST_CHECK(run, UIThemeStyle_GetBox(Toggle, UITogglePart_Check)->Texture == NULL); // a rectangle past the texture's edge

            const UIThemeStyle* Slider = UITheme_FindStyle(Theme, UIElementKind_Slider, NULL);
            TEST_CHECK(run, UIThemeStyle_GetBox(Slider, UISliderPart_Track)->Texture != NULL); // The good part is untouched.
            TEST_CHECK(run, UIThemeStyle_GetBox(Slider, UISliderPart_Thumb)->Texture == NULL); // One state's rectangle past the edge.
        }
        TestAssetFixture_Deconstruct(run, &Fixture);
    }
}


// Tests: the nine quads.
static void NineQuadsTileAnyBoxWithoutGaps(TestRun* run)
{
    UIInsets Margins = { .Left = 4.0f, .Top = 3.0f, .Right = 5.0f, .Bottom = 2.0f };
    Rectangle Source = { .x = 8.0f, .y = 16.0f, .width = 24.0f, .height = 16.0f };
    const float Scales[] = { 0.5f, 1.0f, 1.25f, 1.5f, 2.0f, 3.3f };
    for (size_t ScaleIndex = 0U; ScaleIndex < (sizeof(Scales) / sizeof(Scales[0])); ScaleIndex++)
    {
        for (int Width = 0; Width <= 67; Width++)
        {
            for (int Height = 0; Height <= 37; Height += 3)
            {
                Rectangle Box = { .x = 17.0f, .y = 23.0f, .width = (float)Width, .height = (float)Height };
                UINineSliceQuad Quads[UI_NINE_SLICE_QUAD_COUNT];
                UINineSlice_GetQuads(Box, Source, Margins, Scales[ScaleIndex], Quads);
                TEST_CHECK(run, DoQuadsTileTheBox(Quads, Box));

                // Every edge is whole.
                for (size_t Index = 0U; Index < UI_NINE_SLICE_QUAD_COUNT; Index++)
                {
                    const Rectangle* Quad = &Quads[Index].Destination;
                    TEST_CHECK(run, (Quad->x == roundf(Quad->x)) && (Quad->y == roundf(Quad->y))
                        && ((Quad->x + Quad->width) == roundf(Quad->x + Quad->width)) && ((Quad->y + Quad->height) == roundf(Quad->y + Quad->height)));
                }
            }
        }
    }
}

static void CornersKeepTheirSizeAndTheCentreStretches(TestRun* run)
{
    UIInsets Margins = { .Left = 4.0f, .Top = 3.0f, .Right = 5.0f, .Bottom = 2.0f };
    Rectangle Source = { .x = 8.0f, .y = 16.0f, .width = 24.0f, .height = 16.0f };
    Rectangle Box = { .x = 10.0f, .y = 20.0f, .width = 100.0f, .height = 50.0f };
    UINineSliceQuad Quads[UI_NINE_SLICE_QUAD_COUNT];
    UINineSlice_GetQuads(Box, Source, Margins, 2.0f, Quads);

    // The destination: corners at twice their texels.
    TEST_CHECK(run, TestUI_IsBox(Quads[0].Destination, 10.0f, 20.0f, 8.0f, 6.0f));
    TEST_CHECK(run, TestUI_IsBox(Quads[2].Destination, 100.0f, 20.0f, 10.0f, 6.0f));
    TEST_CHECK(run, TestUI_IsBox(Quads[6].Destination, 10.0f, 66.0f, 8.0f, 4.0f));
    TEST_CHECK(run, TestUI_IsBox(Quads[8].Destination, 100.0f, 66.0f, 10.0f, 4.0f));
    TEST_CHECK(run, TestUI_IsBox(Quads[4].Destination, 18.0f, 26.0f, 82.0f, 40.0f));

    // The source: the same nine parts of the art, each rectangle where the margins say.
    TEST_CHECK(run, TestUI_IsBox(Quads[0].Source, 8.0f, 16.0f, 4.0f, 3.0f));
    TEST_CHECK(run, TestUI_IsBox(Quads[1].Source, 12.0f, 16.0f, 15.0f, 3.0f));
    TEST_CHECK(run, TestUI_IsBox(Quads[2].Source, 27.0f, 16.0f, 5.0f, 3.0f));
    TEST_CHECK(run, TestUI_IsBox(Quads[3].Source, 8.0f, 19.0f, 4.0f, 11.0f));
    TEST_CHECK(run, TestUI_IsBox(Quads[4].Source, 12.0f, 19.0f, 15.0f, 11.0f));
    TEST_CHECK(run, TestUI_IsBox(Quads[5].Source, 27.0f, 19.0f, 5.0f, 11.0f));
    TEST_CHECK(run, TestUI_IsBox(Quads[8].Source, 27.0f, 30.0f, 5.0f, 2.0f));
}

/* A box smaller than its corners: both sides of the axis shrink by one factor, the centre goes, nothing runs out of it. */
static void OversizedMarginsShrinkEvenly(TestRun* run)
{
    UIInsets Margins = { .Left = 6.0f, .Top = 4.0f, .Right = 2.0f, .Bottom = 4.0f };
    Rectangle Source = { .x = 0.0f, .y = 0.0f, .width = 16.0f, .height = 16.0f };
    Rectangle Box = { .x = 0.0f, .y = 0.0f, .width = 4.0f, .height = 20.0f };
    UINineSliceQuad Quads[UI_NINE_SLICE_QUAD_COUNT];
    UINineSlice_GetQuads(Box, Source, Margins, 1.0f, Quads);
    TEST_CHECK(run, DoQuadsTileTheBox(Quads, Box));
    TEST_CHECK(run, TestUI_IsBox(Quads[0].Destination, 0.0f, 0.0f, 3.0f, 4.0f)); // 6 : 2 of 4 wide: 3 and 1
    TEST_CHECK(run, TestUI_IsBox(Quads[2].Destination, 3.0f, 0.0f, 1.0f, 4.0f));
    TEST_CHECK(run, Quads[1].Destination.width == 0.0f);
    TEST_CHECK(run, Quads[4].Destination.width == 0.0f);
    // The sources stay whole: the corners are squeezed, not cut.
    TEST_CHECK(run, TestUI_IsBox(Quads[0].Source, 0.0f, 0.0f, 6.0f, 4.0f));

    // They shrink by what fits, not by a fixed share: 12 : 4 of 12 wide is 9 and 3.
    UIInsets Uneven = { .Left = 12.0f, .Top = 0.0f, .Right = 4.0f, .Bottom = 0.0f };
    Rectangle Narrow = { .x = 0.0f, .y = 0.0f, .width = 12.0f, .height = 20.0f };
    UINineSlice_GetQuads(Narrow, Source, Uneven, 1.0f, Quads);
    TEST_CHECK(run, DoQuadsTileTheBox(Quads, Narrow));
    TEST_CHECK(run, Quads[0].Destination.width == 9.0f);
    TEST_CHECK(run, Quads[2].Destination.width == 3.0f);

    // Margins that do not fit the art count as what fits, and hostile numbers give empty quads, never a NaN.
    UIInsets Huge = { .Left = 100.0f, .Top = 100.0f, .Right = 100.0f, .Bottom = 100.0f };
    UINineSlice_GetQuads((Rectangle){ .x = 0.0f, .y = 0.0f, .width = 64.0f, .height = 64.0f }, Source, Huge, 1.0f, Quads);
    TEST_CHECK(run, Quads[0].Source.width == 16.0f);
    TEST_CHECK(run, Quads[2].Source.width == 0.0f);
    TEST_CHECK(run, Quads[1].Source.width == 0.0f);

    const float Hostile[] = { NAN, INFINITY, -1.0f, 0.0f };
    for (size_t Index = 0U; Index < (sizeof(Hostile) / sizeof(Hostile[0])); Index++)
    {
        Rectangle Bad = { .x = 0.0f, .y = 0.0f, .width = Hostile[Index], .height = 10.0f };
        UINineSlice_GetQuads(Bad, Source, Margins, 1.0f, Quads);
        TEST_CHECK(run, (Quads[4].Destination.width == 0.0f) && isfinite(Quads[4].Destination.x));
        Rectangle Nowhere = { .x = Hostile[Index], .y = Hostile[Index], .width = 64.0f, .height = 64.0f };
        Rectangle Unsure = { .x = Hostile[Index], .y = Hostile[Index], .width = 16.0f, .height = 16.0f };
        UINineSlice_GetQuads(Nowhere, Unsure, Margins, 1.0f, Quads);
        TEST_CHECK(run, isfinite(Quads[0].Destination.x) && isfinite(Quads[0].Destination.y) && isfinite(Quads[8].Destination.width));
        TEST_CHECK(run, isfinite(Quads[0].Source.x) && isfinite(Quads[0].Source.y) && isfinite(Quads[8].Source.width));
        UINineSlice_GetQuads((Rectangle){ .x = 0.0f, .y = 0.0f, .width = 64.0f, .height = 64.0f }, Source, Margins, Hostile[Index], Quads);
        TEST_CHECK(run, DoQuadsTileTheBox(Quads, (Rectangle){ .x = 0.0f, .y = 0.0f, .width = 64.0f, .height = 64.0f }));
        TEST_CHECK(run, Quads[0].Destination.width == 6.0f); // A scale that means nothing counts as 1.
        UINineSlice_GetQuads((Rectangle){ .x = 0.0f, .y = 0.0f, .width = 64.0f, .height = 64.0f },
            (Rectangle){ .x = 0.0f, .y = 0.0f, .width = Hostile[Index], .height = 16.0f }, Margins, 1.0f, Quads);
        TEST_CHECK(run, isfinite(Quads[4].Source.width) && isfinite(Quads[4].Destination.width));
    }
}


// Tests: a screen's theme.
static void AScreensThemeSwapsAndReleasesTheOldOne(TestRun* run)
{
    TestUIFixture Fixture;
    TestAssetCounters Counters = { 0 };
    if (TestUI_Construct(run, &Fixture))
    {
        TEST_CHECK(run, TestAssets_AddTexture(run, &Fixture.Assets, "art", 7U, &Counters));
        TEST_CHECK(run, TestUI_SetTheme(run, &Fixture, "{\"name\": \"arty\", \"elements\": {\"button\": {\"background\": {\"kind\": \"nine_slice\", \"texture\": \"art\", \"margins\": 1}}}}"));
        const UITheme* First = UIScreen_GetTheme(&Fixture.Screen);
        TEST_CHECK(run, Counters.LoadCount == 1U);
        uint64_t Generation = UIScreen_GetMeasureGeneration(&Fixture.Screen);
        uint64_t Revision = UIScreen_GetThemeRevision(&Fixture.Screen);

        // The same theme again changes nothing, and holds nothing more.
        TEST_CHECK_SUCCESS(run, UIScreen_SetTheme(&Fixture.Screen, Fixture.Assets.Manager, u8"arty"));
        TEST_CHECK(run, UIScreen_GetTheme(&Fixture.Screen) == First);
        TEST_CHECK(run, UIScreen_GetThemeRevision(&Fixture.Screen) == Revision);

        // Another one: its texture's holder is gone with the old theme, and sizes are measured again.
        TEST_CHECK(run, TestUI_SetTheme(run, &Fixture, "{\"name\": \"plain\"}"));
        TEST_CHECK(run, UIScreen_GetTheme(&Fixture.Screen) != First);
        TEST_CHECK(run, Counters.UnloadCount == 1U);
        TEST_CHECK(run, UIScreen_GetMeasureGeneration(&Fixture.Screen) != Generation);
        TEST_CHECK(run, UIScreen_GetThemeRevision(&Fixture.Screen) != Revision);

        // A theme that does not load leaves what is there.
        const UITheme* Current = UIScreen_GetTheme(&Fixture.Screen);
        TEST_CHECK_SUCCESS(run, UIScreen_SetTheme(&Fixture.Screen, Fixture.Assets.Manager, u8"nobody"));
        TEST_CHECK(run, UIScreen_GetTheme(&Fixture.Screen) == Current);

        // Back, and away: the art comes and goes with the theme, and the screen's teardown leaves nothing held.
        TEST_CHECK_SUCCESS(run, UIScreen_SetTheme(&Fixture.Screen, Fixture.Assets.Manager, u8"arty"));
        TEST_CHECK(run, (Counters.LoadCount == 2U) && (Counters.UnloadCount == 1U));
        TEST_CHECK_SUCCESS(run, UIScreen_ClearTheme(&Fixture.Screen));
        TEST_CHECK(run, UIScreen_GetTheme(&Fixture.Screen) == NULL);
        TEST_CHECK(run, Counters.UnloadCount == 2U);
        TEST_CHECK_SUCCESS(run, UIScreen_ClearTheme(&Fixture.Screen));

        TEST_CHECK_SUCCESS(run, UIScreen_SetTheme(&Fixture.Screen, Fixture.Assets.Manager, u8"arty"));
        TEST_CHECK(run, Counters.LoadCount == 3U);
        TEST_CHECK_SUCCESS(run, UIScreen_Deconstruct(&Fixture.Screen));
        TEST_CHECK(run, Counters.UnloadCount == 3U);
        TEST_CHECK_SUCCESS(run, UIScreen_Construct(&Fixture.Screen, &Fixture.Widgets, NULL));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ThemeCallsRefuseWhatIsMissing(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        TEST_CHECK_ERROR(run, UIScreen_SetTheme(NULL, Fixture.Assets.Manager, u8"a"), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, UIScreen_SetTheme(&Fixture.Screen, NULL, u8"a"), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, UIScreen_SetTheme(&Fixture.Screen, Fixture.Assets.Manager, NULL), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, UIScreen_ClearTheme(NULL), ErrorCode_IllegalArgument);
        TEST_CHECK(run, UIScreen_GetTheme(&Fixture.Screen) == NULL);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A missing theme or part is logged once, however many elements show it and however long. */
static void AMissingPartIsLoggedOnce(TestRun* run)
{
    GenericBuffer Directory;
    GenericBuffer_AllocateVariable(&Directory, 0U, sizeof(unsigned char));
    Logger Log;
    TestUIFixture Fixture;
    if (TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "ui_theme_missing_part", &Directory))
        && TEST_CHECK_SUCCESS(run, Logger_Construct(&Log, Directory._data)))
    {
        if (TestUI_ConstructLogged(run, &Fixture, &Log))
        {
            for (int Index = 0; Index < 3; Index++)
            {
                UIButtonOptions Options = UIButtonOptions_Create(u8"a");
                Options.Element.HasPlacement = true;
                uint64_t Id = WIDGET_ID_INVALID;
                TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Id));
            }
            TestUI_Ticks(run, &Fixture, 5U);
            TEST_CHECK(run, CountInLog(run, Directory._data, "has no theme") == 1U);

            // A theme that leaves the button's background out: once for it, once for the text it leaves out too.
            TEST_CHECK(run, TestUI_SetTheme(run, &Fixture, "{\"name\": \"sparse\", \"elements\": {\"button\": {\"min_size\": [1, 1]}}}"));
            TestUI_Ticks(run, &Fixture, 5U);
            TEST_CHECK(run, CountInLog(run, Directory._data, "has no \"background\" for the element \"button\"") == 1U);
            TEST_CHECK(run, CountInLog(run, Directory._data, "has no \"text\" for the element \"button\"") == 1U);

            // A new theme can leave it out again, and that is news.
            TEST_CHECK(run, TestUI_SetTheme(run, &Fixture, "{\"name\": \"other\"}"));
            TestUI_Ticks(run, &Fixture, 5U);
            TEST_CHECK(run, CountInLog(run, Directory._data, "has no \"background\" for the element \"button\"") == 2U);

            // No theme again: that is news too.
            TEST_CHECK_SUCCESS(run, UIScreen_ClearTheme(&Fixture.Screen));
            TestUI_Ticks(run, &Fixture, 5U);
            TEST_CHECK(run, CountInLog(run, Directory._data, "has no theme") == 2U);
        }
        TestUI_Deconstruct(run, &Fixture);
        TEST_CHECK_SUCCESS(run, Logger_Deconstruct(&Log));
    }
    Memory_Free(Directory._data);
}

/* A kind the theme leaves out, a part it leaves out and a part of kind "none" are three things: only "none" is on purpose. */
static void APartLeftOutAndAnEmptyPartDiffer(TestRun* run)
{
    TestAssetFixture Fixture;
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (TestAssetFixture_Construct(run, &Fixture))
    {
        const UITheme* Theme = LoadTheme(run, &Fixture, "{\"name\": \"t\", \"elements\": {\"panel\": {\"background\": {\"kind\": \"none\"}}, \"button\": {\"min_size\": [1, 1]}}}", "t", &User);
        if (TEST_CHECK(run, Theme != NULL))
        {
            TEST_CHECK(run, UIThemeStyle_GetBox(UITheme_FindStyle(Theme, UIElementKind_Panel, NULL), UIPanelPart_Background)->Kind == UIBoxKind_None);
            TEST_CHECK(run, UIThemeStyle_GetBox(UITheme_FindStyle(Theme, UIElementKind_Button, NULL), UIButtonPart_Background) == NULL);
            TEST_CHECK(run, UITheme_FindStyle(Theme, UIElementKind_Slider, NULL) == NULL);
        }
        TestAssetFixture_Deconstruct(run, &Fixture);
    }
}


// Public functions.
void UIThemeTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "a_minimal_theme_gets_the_defaults", AMinimalThemeGetsTheDefaults },
        { "no_theme_at_all_answers_with_the_defaults", NoThemeAtAllAnswersWithTheDefaults },
        { "every_key_of_a_button_is_read", EveryKeyOfAButtonIsRead },
        { "a_state_left_out_falls_back_to_normal", AStateLeftOutFallsBackToNormal },
        { "a_variant_changes_only_what_it_names", AVariantChangesOnlyWhatItNames },
        { "an_empty_variants_block_is_no_variants", AnEmptyVariantsBlockIsNoVariants },
        { "a_part_of_another_kind_counts_as_left_out", APartOfAnotherKindCountsAsLeftOut },
        { "every_rule_is_refused_with_its_message", EveryRuleIsRefusedWithItsMessage },
        { "palette_rules_are_refused_naming_where", PaletteRulesAreRefusedNamingWhere },
        { "custom_rules_are_refused_naming_where", CustomRulesAreRefusedNamingWhere },
        { "hostile_sizes_and_counts_are_refused", HostileSizesAndCountsAreRefused },
        { "palette_and_custom_counts_have_their_limits", PaletteAndCustomCountsHaveTheirLimits },
        { "palette_colors_resolve_everywhere", PaletteColorsResolveEverywhere },
        { "custom_values_of_every_kind_are_read", CustomValuesOfEveryKindAreRead },
        { "a_variant_merges_custom_values_by_key", AVariantMergesCustomValuesByKey },
        { "a_custom_nine_slice_gets_its_texture", ACustomNineSliceGetsItsTexture },
        { "a_kind_names_its_look", AKindNamesItsLook },
        { "the_tooltip_delay_has_its_bounds_and_default", TheTooltipDelayHasItsBoundsAndDefault },
        { "the_tooltip_kind_has_its_parts", TheTooltipKindHasItsParts },
        { "the_shipped_themes_load_and_resolve", TheShippedThemesLoadAndResolve },
        { "the_light_theme_is_the_flat_one_with_another_palette", TheLightThemeIsTheFlatOneWithAnotherPalette },
        { "a_nine_slice_reads_its_art_and_its_font", ANineSliceReadsItsArtAndItsFont },
        { "a_variant_can_change_a_boxs_kind", AVariantCanChangeABoxsKind },
        { "art_that_cannot_be_used_leaves_only_its_part_plain", ArtThatCannotBeUsedLeavesOnlyItsPartPlain },
        { "nine_quads_tile_any_box_without_gaps", NineQuadsTileAnyBoxWithoutGaps },
        { "corners_keep_their_size_and_the_centre_stretches", CornersKeepTheirSizeAndTheCentreStretches },
        { "oversized_margins_shrink_evenly", OversizedMarginsShrinkEvenly },
        { "a_screens_theme_swaps_and_releases_the_old_one", AScreensThemeSwapsAndReleasesTheOldOne },
        { "theme_calls_refuse_what_is_missing", ThemeCallsRefuseWhatIsMissing },
        { "a_boxs_numbers_have_their_defaults", ABoxsNumbersHaveTheirDefaults },
        { "a_missing_part_is_logged_once", AMissingPartIsLoggedOnce },
        { "a_part_left_out_and_an_empty_part_differ", APartLeftOutAndAnEmptyPartDiffer },
    };
    TestRun_RunSuite(run, "UITheme", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
