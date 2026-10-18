#include "TestSuites.h"
#include "TestAssets.h"
#include "TestText.h"
#include "TextLayout.h"
#include "TextDocument.h"
#include "TextComponentFactory.h"
#include <math.h>


// Macros.
/* Every test glyph advances 5 px at a base size of 10 px, so at the default size 0.1 a glyph is 0.05 wide. */
#define GLYPH_ADVANCE_PIXELS (5)
#define GLYPH_WIDTH (0.05f)
#define LINE_HEIGHT (0.1f)


// Types.
typedef struct LayoutFixtureStruct
{
    TestAssetFixture Assets;
    TextComponentFactory Factory;
    TextDocument Document;
    TextLayout Layout;
    bool IsReady;
} LayoutFixture;


// Static functions.
static bool IsNear(float value, float expected)
{
    return fabsf(value - expected) < 1e-5f;
}

static bool LayoutFixture_Construct(TestRun* run, LayoutFixture* self)
{
    Memory_Zero(self, sizeof(*self));
    self->IsReady = TestAssetFixture_Construct(run, &self->Assets)
        && TestAssets_AddFont(run, &self->Assets, "mono", GLYPH_ADVANCE_PIXELS, NULL)
        && TestAssets_AddFont(run, &self->Assets, "wide", 2 * GLYPH_ADVANCE_PIXELS, NULL)
        && TestAssets_AddSpriteAnimation(run, &self->Assets, "coin_spin", NULL)
        && TEST_CHECK_SUCCESS(run, TextComponentFactory_Construct(&self->Factory))
        && TEST_CHECK_SUCCESS(run, TextDocument_Construct(&self->Document, &self->Factory, self->Assets.Manager, NULL))
        && TEST_CHECK_SUCCESS(run, TextDocument_SetDefaultFontName(&self->Document, (const unsigned char*)u8"mono"))
        && TEST_CHECK_SUCCESS(run, TextLayout_Construct(&self->Layout));
    return self->IsReady;
}

static void LayoutFixture_Deconstruct(TestRun* run, LayoutFixture* self)
{
    TEST_CHECK_SUCCESS(run, TextLayout_Deconstruct(&self->Layout));
    TEST_CHECK_SUCCESS(run, TextDocument_Deconstruct(&self->Document));
    TEST_CHECK(run, TextComponentFactory_GetLiveComponentCount(&self->Factory) == 0U);
    TEST_CHECK_SUCCESS(run, TextComponentFactory_Deconstruct(&self->Factory));
    TestAssetFixture_Deconstruct(run, &self->Assets);
}

// Makes a single string component with the text the document's tree.
static StringComponent* SetText(TestRun* run, LayoutFixture* fixture, const char* text)
{
    StringComponent* String = NULL;
    if (!TEST_CHECK_SUCCESS(run, TextComponentFactory_CreateString(&fixture->Factory, (const unsigned char*)text, &String)))
    {
        return NULL;
    }
    if (!TEST_CHECK_SUCCESS(run, TextDocument_SetRoot(&fixture->Document, StringComponent_AsComponent(String))))
    {
        TEST_CHECK_SUCCESS(run, TextComponentFactory_ReturnTree(&fixture->Factory, StringComponent_AsComponent(String)));
        return NULL;
    }
    return String;
}

// Lays out a single string component and checks the block size and line count.
static void CheckSingleString(TestRun* run, LayoutFixture* fixture, const char* text, const TextLayoutOptions* options,
    float width, float height, size_t lineCount)
{
    if ((SetText(run, fixture, text) != NULL) && TEST_CHECK_SUCCESS(run, TextLayout_Build(&fixture->Layout, &fixture->Document, options)))
    {
        Vector2 Size = TextLayout_GetSize(&fixture->Layout);
        TEST_CHECK(run, IsNear(Size.x, width));
        TEST_CHECK(run, IsNear(Size.y, height));
        TEST_CHECK(run, TextLayout_GetLineCount(&fixture->Layout) == lineCount);
    }
}

static TextLayoutOptions WrapOptions(float maxWidth, bool isWrapEnabled, bool isShrinkEnabled)
{
    TextLayoutOptions Options = TextLayoutOptions_CreateDefault();
    Options.HasMaxWidth = true;
    Options.MaxWidth = maxWidth;
    Options.IsWrapEnabled = isWrapEnabled;
    Options.IsShrinkEnabled = isShrinkEnabled;
    return Options;
}


// Tests.
static void LinesFollowNewlines(TestRun* run)
{
    LayoutFixture Fixture;
    if (LayoutFixture_Construct(run, &Fixture))
    {
        TextLayoutOptions Options = TextLayoutOptions_CreateDefault();
        CheckSingleString(run, &Fixture, "Hello", &Options, 5.0f * GLYPH_WIDTH, LINE_HEIGHT, 1U);
        CheckSingleString(run, &Fixture, "ab\ncdef", &Options, 4.0f * GLYPH_WIDTH, 2.0f * LINE_HEIGHT, 2U);
        // A final newline ends the last line without adding an empty one; an empty line keeps its height.
        CheckSingleString(run, &Fixture, "ab\n", &Options, 2.0f * GLYPH_WIDTH, LINE_HEIGHT, 1U);
        CheckSingleString(run, &Fixture, "a\n\nb", &Options, GLYPH_WIDTH, 3.0f * LINE_HEIGHT, 3U);
        CheckSingleString(run, &Fixture, "\n", &Options, 0.0f, LINE_HEIGHT, 1U);
        CheckSingleString(run, &Fixture, "", &Options, 0.0f, 0.0f, 0U);
        // Tabs are four spaces wide, other control characters are skipped, unknown glyphs draw as '?'.
        CheckSingleString(run, &Fixture, "a\tb", &Options, 6.0f * GLYPH_WIDTH, LINE_HEIGHT, 1U);
        CheckSingleString(run, &Fixture, "a\rb\x01", &Options, 2.0f * GLYPH_WIDTH, LINE_HEIGHT, 1U);
        CheckSingleString(run, &Fixture, "\xC4\x81", &Options, GLYPH_WIDTH, LINE_HEIGHT, 1U);
    }
    LayoutFixture_Deconstruct(run, &Fixture);
}

static void WrappingBreaksAtSpaces(TestRun* run)
{
    LayoutFixture Fixture;
    if (LayoutFixture_Construct(run, &Fixture))
    {
        // "aaa bbb" is 0.35 wide; " ccc" would end at 0.55, so it moves to the next line and its space is dropped.
        TextLayoutOptions Wrap = WrapOptions(0.4f, true, false);
        CheckSingleString(run, &Fixture, "aaa bbb ccc", &Wrap, 7.0f * GLYPH_WIDTH, 2.0f * LINE_HEIGHT, 2U);
        Rectangle Second = TextLayout_GetLineBounds(&Fixture.Layout, 1U);
        TEST_CHECK(run, IsNear(Second.width, 3.0f * GLYPH_WIDTH) && IsNear(Second.y, LINE_HEIGHT));

        // A word that does not fit on its own line overflows; leading spaces after a newline are kept.
        CheckSingleString(run, &Fixture, "aaaaaaaaaa", &Wrap, 10.0f * GLYPH_WIDTH, LINE_HEIGHT, 1U);
        CheckSingleString(run, &Fixture, "  aaaaaaaaaa", &Wrap, 12.0f * GLYPH_WIDTH, LINE_HEIGHT, 1U);

        // Without wrapping the maximum width changes nothing.
        TextLayoutOptions NoWrap = WrapOptions(0.4f, false, false);
        CheckSingleString(run, &Fixture, "aaa bbb ccc", &NoWrap, 11.0f * GLYPH_WIDTH, LINE_HEIGHT, 1U);
    }
    LayoutFixture_Deconstruct(run, &Fixture);
}

static void WrappingKeepsWordsOfSeveralComponentsTogether(TestRun* run)
{
    LayoutFixture Fixture;
    if (LayoutFixture_Construct(run, &Fixture))
    {
        // "aa bb" + "cc" (no space between "bb" and "cc"): the word "bbcc" moves to the next line as a whole.
        StringComponent* Root = SetText(run, &Fixture, "aa bb");
        TextLayoutOptions Wrap = WrapOptions(0.25f, true, false);
        if ((Root != NULL) && (TestText_AddString(run, &Fixture.Factory, StringComponent_AsComponent(Root), "cc") != NULL)
            && TEST_CHECK_SUCCESS(run, TextLayout_Build(&Fixture.Layout, &Fixture.Document, &Wrap)))
        {
            TEST_CHECK(run, TextLayout_GetLineCount(&Fixture.Layout) == 2U);
            TEST_CHECK(run, IsNear(TextLayout_GetLineBounds(&Fixture.Layout, 0U).width, 2.0f * GLYPH_WIDTH));
            TEST_CHECK(run, IsNear(TextLayout_GetLineBounds(&Fixture.Layout, 1U).width, 4.0f * GLYPH_WIDTH));
        }
    }
    LayoutFixture_Deconstruct(run, &Fixture);
}

static void ShrinkingScalesWideLines(TestRun* run)
{
    LayoutFixture Fixture;
    if (LayoutFixture_Construct(run, &Fixture))
    {
        TextLayoutOptions Shrink = WrapOptions(0.25f, false, true);
        CheckSingleString(run, &Fixture, "aaaaaaaaaa", &Shrink, 0.25f, 0.5f * LINE_HEIGHT, 1U);

        // Wrap first, then shrink what still overflows: "aaa" stays, "bbbbbbbb" (0.4) shrinks to 0.3.
        TextLayoutOptions WrapThenShrink = WrapOptions(0.3f, true, true);
        CheckSingleString(run, &Fixture, "aaa bbbbbbbb", &WrapThenShrink, 0.3f, LINE_HEIGHT + 0.075f, 2U);
    }
    LayoutFixture_Deconstruct(run, &Fixture);
}

static void LinesAreAlignedWithinTheBlock(TestRun* run)
{
    LayoutFixture Fixture;
    if (LayoutFixture_Construct(run, &Fixture))
    {
        TextLayoutOptions Options = TextLayoutOptions_CreateDefault();
        Options.Alignment = TextLayoutAlignment_Center;
        CheckSingleString(run, &Fixture, "ab\nabcd", &Options, 4.0f * GLYPH_WIDTH, 2.0f * LINE_HEIGHT, 2U);
        TEST_CHECK(run, IsNear(TextLayout_GetLineBounds(&Fixture.Layout, 0U).x, GLYPH_WIDTH));
        TEST_CHECK(run, IsNear(TextLayout_GetRunBounds(&Fixture.Layout, 0U).x, GLYPH_WIDTH));

        Options.Alignment = TextLayoutAlignment_Right;
        CheckSingleString(run, &Fixture, "ab\nabcd", &Options, 4.0f * GLYPH_WIDTH, 2.0f * LINE_HEIGHT, 2U);
        TEST_CHECK(run, IsNear(TextLayout_GetLineBounds(&Fixture.Layout, 0U).x, 2.0f * GLYPH_WIDTH));
        TEST_CHECK(run, IsNear(TextLayout_GetLineBounds(&Fixture.Layout, 1U).x, 0.0f));
    }
    LayoutFixture_Deconstruct(run, &Fixture);
}

static void RunsAreBottomAlignedAndSpritesTakeTheirSize(TestRun* run)
{
    LayoutFixture Fixture;
    if (LayoutFixture_Construct(run, &Fixture))
    {
        // "Big" at size 0.2 in the wide font, then a 0.1 x 0.3 sprite whose animation is missing, then "s" at 0.1.
        StringComponent* Big = SetText(run, &Fixture, "Big");
        SpriteComponent* Missing = NULL;
        bool IsBuilt = (Big != NULL)
            && TEST_CHECK_SUCCESS(run, StringComponent_SetSize(Big, 0.2f))
            && TEST_CHECK_SUCCESS(run, StringComponent_SetFontName(Big, (const unsigned char*)u8"wide"))
            && TEST_CHECK_SUCCESS(run, TextComponentFactory_CreateSprite(&Fixture.Factory, (const unsigned char*)u8"not_there", &Missing))
            && TEST_CHECK_SUCCESS(run, SpriteComponent_SetSize(Missing, (Vector2){ 0.1f, 0.3f }))
            && TEST_CHECK_SUCCESS(run, TextComponent_AddSubComponent(StringComponent_AsComponent(Big), SpriteComponent_AsComponent(Missing)))
            && (TestText_AddString(run, &Fixture.Factory, StringComponent_AsComponent(Big), "s") != NULL);
        if (!IsBuilt && (Missing != NULL) && (TextComponent_GetParent(SpriteComponent_AsComponent(Missing)) == NULL))
        {
            TEST_CHECK_SUCCESS(run, TextComponentFactory_ReturnTree(&Fixture.Factory, SpriteComponent_AsComponent(Missing)));
        }

        TextLayoutOptions Options = TextLayoutOptions_CreateDefault();
        if (IsBuilt && TEST_CHECK_SUCCESS(run, TextLayout_Build(&Fixture.Layout, &Fixture.Document, &Options)))
        {
            // The wide font at size 0.2 advances 0.2 per glyph: "Big" is 0.6 wide.
            TEST_CHECK(run, TextLayout_GetRunCount(&Fixture.Layout) == 3U);
            Rectangle BigRun = TextLayout_GetRunBounds(&Fixture.Layout, 0U);
            Rectangle Sprite = TextLayout_GetRunBounds(&Fixture.Layout, 1U);
            Rectangle Small = TextLayout_GetRunBounds(&Fixture.Layout, 2U);
            TEST_CHECK(run, IsNear(BigRun.width, 0.6f) && IsNear(BigRun.y, 0.1f));
            TEST_CHECK(run, IsNear(Sprite.x, 0.6f) && IsNear(Sprite.width, 0.1f) && IsNear(Sprite.y, 0.0f));
            TEST_CHECK(run, IsNear(Small.x, 0.7f) && IsNear(Small.y, 0.2f));
            Vector2 Size = TextLayout_GetSize(&Fixture.Layout);
            TEST_CHECK(run, IsNear(Size.x, 0.75f) && IsNear(Size.y, 0.3f));
        }
    }
    LayoutFixture_Deconstruct(run, &Fixture);
}

static void TranslationsShowTheirFallbackOrKey(TestRun* run)
{
    LayoutFixture Fixture;
    if (LayoutFixture_Construct(run, &Fixture))
    {
        TranslateComponent* Translate = NULL;
        TextLayoutOptions Options = TextLayoutOptions_CreateDefault();
        if (TEST_CHECK_SUCCESS(run, TextComponentFactory_CreateTranslate(&Fixture.Factory, (const unsigned char*)u8"menu.title",
            (const unsigned char*)u8"Hi", &Translate)))
        {
            if (TEST_CHECK_SUCCESS(run, TextDocument_SetRoot(&Fixture.Document, TranslateComponent_AsComponent(Translate))))
            {
                TEST_CHECK_SUCCESS(run, TextLayout_Build(&Fixture.Layout, &Fixture.Document, &Options));
                TEST_CHECK(run, IsNear(TextLayout_GetSize(&Fixture.Layout).x, 2.0f * GLYPH_WIDTH));
                TEST_CHECK_SUCCESS(run, TranslateComponent_SetFallback(Translate, NULL));
                TEST_CHECK_SUCCESS(run, TextLayout_Build(&Fixture.Layout, &Fixture.Document, &Options));
                TEST_CHECK(run, IsNear(TextLayout_GetSize(&Fixture.Layout).x, 10.0f * GLYPH_WIDTH));
            }
            else
            {
                TEST_CHECK_SUCCESS(run, TextComponentFactory_ReturnTree(&Fixture.Factory, TranslateComponent_AsComponent(Translate)));
            }
        }
    }
    LayoutFixture_Deconstruct(run, &Fixture);
}

static void SizeMultiplierAndSpacingScaleTheText(TestRun* run)
{
    LayoutFixture Fixture;
    if (LayoutFixture_Construct(run, &Fixture))
    {
        TextLayoutOptions Doubled = TextLayoutOptions_CreateDefault();
        Doubled.SizeMultiplier = 2.0f;
        CheckSingleString(run, &Fixture, "ab", &Doubled, 4.0f * GLYPH_WIDTH, 2.0f * LINE_HEIGHT, 1U);

        // Spacing 0.5 adds half the size after each glyph except the line's last one.
        TextLayoutOptions Options = TextLayoutOptions_CreateDefault();
        StringComponent* Spaced = SetText(run, &Fixture, "ab");
        if ((Spaced != NULL) && TEST_CHECK_SUCCESS(run, StringComponent_SetSpacing(Spaced, 0.5f))
            && TEST_CHECK_SUCCESS(run, TextLayout_Build(&Fixture.Layout, &Fixture.Document, &Options)))
        {
            TEST_CHECK(run, IsNear(TextLayout_GetSize(&Fixture.Layout).x, (2.0f * GLYPH_WIDTH) + (0.5f * LINE_HEIGHT)));
        }

        TextLayoutOptions Invalid = TextLayoutOptions_CreateDefault();
        Invalid.SizeMultiplier = -1.0f;
        TEST_CHECK_ERROR(run, TextLayout_Build(&Fixture.Layout, &Fixture.Document, &Invalid), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, !TextLayout_IsBuilt(&Fixture.Layout));

        // A spacing below the glyph advance stacks the glyphs instead of moving backwards: "abc" at spacing -1 (-0.1
        // per glyph) is as wide as its last glyph.
        StringComponent* Stacked = SetText(run, &Fixture, "abc");
        if ((Stacked != NULL) && TEST_CHECK_SUCCESS(run, StringComponent_SetSpacing(Stacked, -1.0f))
            && TEST_CHECK_SUCCESS(run, TextLayout_Build(&Fixture.Layout, &Fixture.Document, &Options)))
        {
            TEST_CHECK(run, IsNear(TextLayout_GetSize(&Fixture.Layout).x, GLYPH_WIDTH));
            TEST_CHECK(run, IsNear(TextLayout_GetRunBounds(&Fixture.Layout, 0U).x, 0.0f));
        }
    }
    LayoutFixture_Deconstruct(run, &Fixture);
}

// A line ends at its last glyph: neither the spacing after it nor trailing spaces count, for wrapping, shrinking or
// alignment.
static void LinesEndAtTheirLastGlyph(TestRun* run)
{
    LayoutFixture Fixture;
    if (LayoutFixture_Construct(run, &Fixture))
    {
        TextLayoutOptions Options = TextLayoutOptions_CreateDefault();
        CheckSingleString(run, &Fixture, "ab   ", &Options, 2.0f * GLYPH_WIDTH, LINE_HEIGHT, 1U);
        CheckSingleString(run, &Fixture, "   ", &Options, 0.0f, LINE_HEIGHT, 1U);
        Options.Alignment = TextLayoutAlignment_Center;
        CheckSingleString(run, &Fixture, "ab  \nabcd", &Options, 4.0f * GLYPH_WIDTH, 2.0f * LINE_HEIGHT, 2U);
        TEST_CHECK(run, IsNear(TextLayout_GetLineBounds(&Fixture.Layout, 0U).x, GLYPH_WIDTH));

        // Trailing spaces never trigger shrinking.
        TextLayoutOptions Shrink = WrapOptions(2.0f * GLYPH_WIDTH + 0.001f, false, true);
        CheckSingleString(run, &Fixture, "ab      ", &Shrink, 2.0f * GLYPH_WIDTH, LINE_HEIGHT, 1U);

        // With spacing 0.5 (0.05 after each glyph) "aa bb" measures 0.45: it fits 0.47 although its advances add up
        // to 0.5.
        StringComponent* Spaced = SetText(run, &Fixture, "aa bb");
        TextLayoutOptions Wrap = WrapOptions(0.47f, true, false);
        if ((Spaced != NULL) && TEST_CHECK_SUCCESS(run, StringComponent_SetSpacing(Spaced, 0.5f))
            && TEST_CHECK_SUCCESS(run, TextLayout_Build(&Fixture.Layout, &Fixture.Document, &Wrap)))
        {
            TEST_CHECK(run, TextLayout_GetLineCount(&Fixture.Layout) == 1U);
            TEST_CHECK(run, IsNear(TextLayout_GetSize(&Fixture.Layout).x, 0.45f));
        }
    }
    LayoutFixture_Deconstruct(run, &Fixture);
}

// C0 and C1 control characters and DEL are skipped; other codepoints, even ones the font lacks, take a glyph.
static void ControlCharactersAreSkipped(TestRun* run)
{
    LayoutFixture Fixture;
    if (LayoutFixture_Construct(run, &Fixture))
    {
        TextLayoutOptions Options = TextLayoutOptions_CreateDefault();
        CheckSingleString(run, &Fixture, "a\xC2\x85" "b\xC2\x9F\x7F\x1B", &Options, 2.0f * GLYPH_WIDTH, LINE_HEIGHT, 1U);
        CheckSingleString(run, &Fixture, "a\xC2\xA0" "b", &Options, 3.0f * GLYPH_WIDTH, LINE_HEIGHT, 1U);
    }
    LayoutFixture_Deconstruct(run, &Fixture);
}

static bool IsNearVector(Vector2 value, float x, float y)
{
    return IsNear(value.x, x) && IsNear(value.y, y);
}

// A string's shadow is drawn at its own offset, or at its font's when it sets none, times its size.
static void ShadowsUseTheFontOffsetUnlessSet(TestRun* run)
{
    LayoutFixture Fixture;
    if (LayoutFixture_Construct(run, &Fixture)
        && TestAssets_AddFontWithShadowOffset(run, &Fixture.Assets, "thin", GLYPH_ADVANCE_PIXELS, (Vector2){ -0.05f, 0.05f }, NULL))
    {
        // "a" in the document's default font (mono: the default offset), "b" in thin, "c" in thin with its own offset,
        // and "d" without a shadow.
        StringComponent* Root = SetText(run, &Fixture, "a");
        TextComponent* RootBase = (Root != NULL) ? StringComponent_AsComponent(Root) : NULL;
        StringComponent* Thin = (RootBase != NULL) ? TestText_AddString(run, &Fixture.Factory, RootBase, "b") : NULL;
        StringComponent* Own = (Thin != NULL) ? TestText_AddString(run, &Fixture.Factory, RootBase, "c") : NULL;
        StringComponent* None = (Own != NULL) ? TestText_AddString(run, &Fixture.Factory, RootBase, "d") : NULL;
        bool IsBuilt = (None != NULL)
            && TEST_CHECK_SUCCESS(run, StringComponent_SetFontName(Thin, (const unsigned char*)u8"thin"))
            && TEST_CHECK_SUCCESS(run, StringComponent_SetFontName(Own, (const unsigned char*)u8"thin"))
            && TEST_CHECK_SUCCESS(run, StringComponent_SetShadowOffsetCustom(Own, (Vector2){ 0.2f, 0.3f }))
            && TEST_CHECK_SUCCESS(run, StringComponent_SetIsShadowActive(None, false));
        TextLayoutOptions Options = TextLayoutOptions_CreateDefault();
        if (IsBuilt && TEST_CHECK_SUCCESS(run, TextLayout_Build(&Fixture.Layout, &Fixture.Document, &Options)))
        {
            // At the default size 0.1, an offset of 0.1 moves the shadow by 0.01.
            TEST_CHECK(run, TextLayout_GetRunCount(&Fixture.Layout) == 4U);
            TEST_CHECK(run, IsNearVector(TextLayout_GetRunShadowOffset(&Fixture.Layout, 0U), -0.01f, 0.01f));
            TEST_CHECK(run, IsNearVector(TextLayout_GetRunShadowOffset(&Fixture.Layout, 1U), -0.005f, 0.005f));
            TEST_CHECK(run, IsNearVector(TextLayout_GetRunShadowOffset(&Fixture.Layout, 2U), 0.02f, 0.03f));
            TEST_CHECK(run, IsNearVector(TextLayout_GetRunShadowOffset(&Fixture.Layout, 3U), 0.0f, 0.0f));
            TEST_CHECK(run, IsNearVector(TextLayout_GetRunShadowOffset(&Fixture.Layout, 4U), 0.0f, 0.0f));

            // Back to the font's offset: an edit like any other, so the layout rebuilds.
            TEST_CHECK_SUCCESS(run, StringComponent_SetShadowOffsetDefault(Own));
            TEST_CHECK_SUCCESS(run, TextLayout_Refresh(&Fixture.Layout, &Fixture.Document, &Options));
            TEST_CHECK(run, IsNearVector(TextLayout_GetRunShadowOffset(&Fixture.Layout, 2U), -0.005f, 0.005f));
        }
    }
    LayoutFixture_Deconstruct(run, &Fixture);
}

// raylib's built-in font has no glyphs before InitWindow (as in these tests): a layout built with it stays out of date
// so it is rebuilt once the font has them.
static void BuildsWithTheEmptyBuiltInFontStayStale(TestRun* run)
{
    TextComponentFactory Factory;
    TextDocument Document;
    TextLayout Layout;
    if (!TEST_CHECK_SUCCESS(run, TextComponentFactory_Construct(&Factory)))
    {
        return;
    }
    if (TEST_CHECK_SUCCESS(run, TextDocument_Construct(&Document, &Factory, NULL, NULL)))
    {
        if (TEST_CHECK_SUCCESS(run, TextLayout_Construct(&Layout)))
        {
            StringComponent* String = NULL;
            TextLayoutOptions Options = TextLayoutOptions_CreateDefault();
            if (TEST_CHECK_SUCCESS(run, TextComponentFactory_CreateString(&Factory, (const unsigned char*)u8"ab", &String)))
            {
                if (TEST_CHECK_SUCCESS(run, TextDocument_SetRoot(&Document, StringComponent_AsComponent(String))))
                {
                    TEST_CHECK_SUCCESS(run, TextLayout_Refresh(&Layout, &Document, &Options));
                    TEST_CHECK(run, TextLayout_IsBuilt(&Layout));
                    TEST_CHECK(run, !TextLayout_IsUpToDate(&Layout, &Document, &Options));
                }
                else
                {
                    TEST_CHECK_SUCCESS(run, TextComponentFactory_ReturnTree(&Factory, StringComponent_AsComponent(String)));
                }
            }
            TEST_CHECK_SUCCESS(run, TextLayout_Deconstruct(&Layout));
        }
        TEST_CHECK_SUCCESS(run, TextDocument_Deconstruct(&Document));
    }
    TEST_CHECK(run, TextComponentFactory_GetLiveComponentCount(&Factory) == 0U);
    TEST_CHECK_SUCCESS(run, TextComponentFactory_Deconstruct(&Factory));
}

static void RefreshRebuildsOnlyWhenSomethingChanged(TestRun* run)
{
    LayoutFixture Fixture;
    if (LayoutFixture_Construct(run, &Fixture))
    {
        StringComponent* Root = SetText(run, &Fixture, "ab");
        StringComponent* Child = (Root != NULL) ? TestText_AddString(run, &Fixture.Factory, StringComponent_AsComponent(Root), "cd") : NULL;
        TextLayoutOptions Options = TextLayoutOptions_CreateDefault();
        if ((Child != NULL) && TEST_CHECK_SUCCESS(run, TextLayout_Refresh(&Fixture.Layout, &Fixture.Document, &Options)))
        {
            TEST_CHECK(run, TextLayout_IsUpToDate(&Fixture.Layout, &Fixture.Document, &Options));
            TEST_CHECK(run, IsNear(TextLayout_GetSize(&Fixture.Layout).x, 4.0f * GLYPH_WIDTH));

            // An edit deep in the tree, an option change and a new root each make the layout stale.
            TEST_CHECK_SUCCESS(run, StringComponent_SetText(Child, (const unsigned char*)u8"cdef"));
            TEST_CHECK(run, !TextLayout_IsUpToDate(&Fixture.Layout, &Fixture.Document, &Options));
            TEST_CHECK_SUCCESS(run, TextLayout_Refresh(&Fixture.Layout, &Fixture.Document, &Options));
            TEST_CHECK(run, IsNear(TextLayout_GetSize(&Fixture.Layout).x, 6.0f * GLYPH_WIDTH));

            TextLayoutOptions Centered = Options;
            Centered.Alignment = TextLayoutAlignment_Center;
            TEST_CHECK(run, !TextLayout_IsUpToDate(&Fixture.Layout, &Fixture.Document, &Centered));

            // Without HasMaxWidth, the maximum width and what uses it do not matter.
            TextLayoutOptions Unused = Options;
            Unused.MaxWidth = Options.MaxWidth + 1.0f;
            Unused.IsWrapEnabled = !Options.IsWrapEnabled;
            Unused.IsShrinkEnabled = !Options.IsShrinkEnabled;
            TEST_CHECK(run, !Options.HasMaxWidth && TextLayout_IsUpToDate(&Fixture.Layout, &Fixture.Document, &Unused));

            TEST_CHECK(run, SetText(run, &Fixture, "x") != NULL);
            TEST_CHECK(run, !TextLayout_IsUpToDate(&Fixture.Layout, &Fixture.Document, &Options));
            TEST_CHECK_SUCCESS(run, TextLayout_Refresh(&Fixture.Layout, &Fixture.Document, &Options));
            TEST_CHECK(run, IsNear(TextLayout_GetSize(&Fixture.Layout).x, GLYPH_WIDTH));
        }
    }
    LayoutFixture_Deconstruct(run, &Fixture);
}


// Public functions.
void TextLayoutTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "lines_follow_newlines", LinesFollowNewlines },
        { "wrapping_breaks_at_spaces", WrappingBreaksAtSpaces },
        { "wrapping_keeps_words_of_several_components_together", WrappingKeepsWordsOfSeveralComponentsTogether },
        { "shrinking_scales_wide_lines", ShrinkingScalesWideLines },
        { "lines_are_aligned_within_the_block", LinesAreAlignedWithinTheBlock },
        { "runs_are_bottom_aligned_and_sprites_take_their_size", RunsAreBottomAlignedAndSpritesTakeTheirSize },
        { "translations_show_their_fallback_or_key", TranslationsShowTheirFallbackOrKey },
        { "size_multiplier_and_spacing_scale_the_text", SizeMultiplierAndSpacingScaleTheText },
        { "refresh_rebuilds_only_when_something_changed", RefreshRebuildsOnlyWhenSomethingChanged },
        { "lines_end_at_their_last_glyph", LinesEndAtTheirLastGlyph },
        { "control_characters_are_skipped", ControlCharactersAreSkipped },
        { "shadows_use_the_font_offset_unless_set", ShadowsUseTheFontOffsetUnlessSet },
        { "builds_with_the_empty_built_in_font_stay_stale", BuildsWithTheEmptyBuiltInFontStayStale },
    };
    TestRun_RunSuite(run, "TextLayout", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
