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
/** How many bytes a refusal test's JSON may take. */
#define THEME_TEXT_CAPACITY ((size_t)1024)
/** Room for a log's path and text. */
#define LOG_BUFFER_CAPACITY ((size_t)256)


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
            TEST_CHECK(run, UITheme_FindStyle(Theme, UIElementKind_Button, NULL) == NULL);
        }
        TestAssetFixture_Deconstruct(run, &Fixture);
    }
}

static void NoThemeAtAllAnswersWithTheDefaults(TestRun* run)
{
    TEST_CHECK(run, UITheme_GetFontName(NULL) == NULL);
    TEST_CHECK(run, UITheme_GetTextSize(NULL) == UI_THEME_DEFAULT_TEXT_SIZE);
    TEST_CHECK(run, UITheme_GetTransitionSeconds(NULL) == 0.0f);
    TEST_CHECK(run, UITheme_FindStyle(NULL, UIElementKind_Button, NULL) == NULL);
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
    };
    TestAssetFixture Fixture;
    if (TestAssetFixture_Construct(run, &Fixture))
    {
        for (size_t Index = 0U; Index < (sizeof(Refusals) / sizeof(Refusals[0])); Index++)
        {
            const char* Parts[] = { Refusals[Index].MessagePart, NULL };
            Error Result = BuildTheme(&Fixture, Refusals[Index].Json);
            if (!TestText_CheckErrorMessage(run, Result, ErrorCode_InvalidAssetDefinition, Parts))
            {
                printf("    in refusal %zu: %s\n", Index, Refusals[Index].Json);
            }
        }
        TestAssetFixture_Deconstruct(run, &Fixture);
    }
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
        { "hostile_sizes_and_counts_are_refused", HostileSizesAndCountsAreRefused },
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
