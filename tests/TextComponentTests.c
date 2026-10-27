#include "TestSuites.h"
#include "TestText.h"
#include "TextComponent.h"
#include "TextComponentFactory.h"
#include "TextComponentText.h"
#include "TextStyle.h"
#include <math.h>
#include <string.h>


// Static functions.
static bool TextEquals(const unsigned char* text, const char* expected)
{
    return strcmp((const char*)text, expected) == 0;
}

static EmptyComponent* CreateEmpty(TestRun* run, TextComponentFactory* factory)
{
    EmptyComponent* Empty = NULL;
    TEST_CHECK_SUCCESS(run, TextComponentFactory_CreateEmpty(factory, &Empty));
    return Empty;
}

static void ReturnTree(TestRun* run, TextComponentFactory* factory, TextComponent* root)
{
    if (root != NULL)
    {
        TEST_CHECK_SUCCESS(run, TextComponentFactory_ReturnTree(factory, root));
    }
}

// Builds a chain of levelCount components (root first); *outDeepest receives the last one.
static TextComponent* BuildChain(TestRun* run, TextComponentFactory* factory, size_t levelCount, TextComponent** outDeepest)
{
    EmptyComponent* Root = CreateEmpty(run, factory);
    if (Root == NULL)
    {
        return NULL;
    }
    TextComponent* Deepest = EmptyComponent_AsComponent(Root);
    for (size_t Level = 1; Level < levelCount; Level++)
    {
        EmptyComponent* Child = CreateEmpty(run, factory);
        if ((Child == NULL) || !TEST_CHECK_SUCCESS(run, TextComponent_AddSubComponent(Deepest, EmptyComponent_AsComponent(Child))))
        {
            ReturnTree(run, factory, (Child != NULL) ? EmptyComponent_AsComponent(Child) : NULL);
            break;
        }
        Deepest = EmptyComponent_AsComponent(Child);
    }
    *outDeepest = Deepest;
    return EmptyComponent_AsComponent(Root);
}


// Tests: the strict tree.
static void CreatedComponentIsARootWithDefaults(TestRun* run)
{
    TextComponentFactory Factory;
    if (!TEST_CHECK_SUCCESS(run, TextComponentFactory_Construct(&Factory)))
    {
        return;
    }
    StringComponent* String = NULL;
    if (TEST_CHECK_SUCCESS(run, TextComponentFactory_CreateString(&Factory, (const unsigned char*)u8"Hello", &String)))
    {
        TextComponent* Base = StringComponent_AsComponent(String);
        TEST_CHECK(run, TextComponent_GetParent(Base) == NULL);
        TEST_CHECK(run, TextComponent_GetHeight(Base) == 1U);
        TEST_CHECK(run, TextComponent_GetDepth(Base) == 1U);
        TEST_CHECK(run, TextEquals(StringComponent_GetText(String), "Hello"));
        TEST_CHECK(run, StringComponent_GetFontName(String) == NULL);
        TEST_CHECK(run, StringComponent_GetSize(String) == STRING_COMPONENT_DEFAULT_SIZE);
        TEST_CHECK(run, StringComponent_IsShadowActive(String));
        TEST_CHECK(run, StringComponent_GetShadowOffsetType(String) == TextShadowOffsetType_Default);
        TEST_CHECK(run, TextComponentFactory_GetLiveComponentCount(&Factory) == 1U);
        ReturnTree(run, &Factory, Base);
    }
    TEST_CHECK(run, TextComponentFactory_GetLiveComponentCount(&Factory) == 0U);
    TEST_CHECK_SUCCESS(run, TextComponentFactory_Deconstruct(&Factory));
}

static void AddRejectsAChildThatHasAParent(TestRun* run)
{
    TextComponentFactory Factory;
    if (!TEST_CHECK_SUCCESS(run, TextComponentFactory_Construct(&Factory)))
    {
        return;
    }
    EmptyComponent* First = CreateEmpty(run, &Factory);
    EmptyComponent* Second = CreateEmpty(run, &Factory);
    StringComponent* Child = (First != NULL) ? TestText_AddString(run, &Factory, EmptyComponent_AsComponent(First), "c") : NULL;
    if ((Second != NULL) && (Child != NULL))
    {
        TEST_CHECK_ERROR(run, TextComponent_AddSubComponent(EmptyComponent_AsComponent(Second), StringComponent_AsComponent(Child)),
            ErrorCode_InvalidOperation);
        TEST_CHECK(run, TextComponent_GetParent(StringComponent_AsComponent(Child)) == EmptyComponent_AsComponent(First));
        TEST_CHECK(run, TextComponent_GetSubComponentCount(EmptyComponent_AsComponent(Second)) == 0U);
    }
    ReturnTree(run, &Factory, (First != NULL) ? EmptyComponent_AsComponent(First) : NULL);
    ReturnTree(run, &Factory, (Second != NULL) ? EmptyComponent_AsComponent(Second) : NULL);
    TEST_CHECK(run, TextComponentFactory_GetLiveComponentCount(&Factory) == 0U);
    TEST_CHECK_SUCCESS(run, TextComponentFactory_Deconstruct(&Factory));
}

static void AddRejectsCyclesAndOtherFactories(TestRun* run)
{
    TextComponentFactory Factory;
    TextComponentFactory Other;
    if (!TEST_CHECK_SUCCESS(run, TextComponentFactory_Construct(&Factory)) || !TEST_CHECK_SUCCESS(run, TextComponentFactory_Construct(&Other)))
    {
        return;
    }
    TextComponent* Deepest = NULL;
    TextComponent* Root = BuildChain(run, &Factory, 3U, &Deepest);
    EmptyComponent* Foreign = CreateEmpty(run, &Other);
    if ((Root != NULL) && (Foreign != NULL))
    {
        TEST_CHECK_ERROR(run, TextComponent_AddSubComponent(Root, Root), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, TextComponent_AddSubComponent(Deepest, Root), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, TextComponent_AddSubComponent(Root, EmptyComponent_AsComponent(Foreign)), ErrorCode_IllegalArgument);
        TEST_CHECK(run, TextComponent_GetHeight(Root) == 3U);
    }
    ReturnTree(run, &Factory, Root);
    ReturnTree(run, &Other, (Foreign != NULL) ? EmptyComponent_AsComponent(Foreign) : NULL);
    TEST_CHECK_SUCCESS(run, TextComponentFactory_Deconstruct(&Factory));
    TEST_CHECK_SUCCESS(run, TextComponentFactory_Deconstruct(&Other));
}

static void DepthLimitIsEnforced(TestRun* run)
{
    TextComponentFactory Factory;
    if (!TEST_CHECK_SUCCESS(run, TextComponentFactory_Construct(&Factory)))
    {
        return;
    }

    TextComponent* Deepest = NULL;
    TextComponent* Root = BuildChain(run, &Factory, TEXT_COMPONENT_MAX_DEPTH, &Deepest);
    if (Root != NULL)
    {
        TEST_CHECK(run, TextComponent_GetHeight(Root) == (size_t)TEXT_COMPONENT_MAX_DEPTH);
        TEST_CHECK(run, TextComponent_GetDepth(Deepest) == (size_t)TEXT_COMPONENT_MAX_DEPTH);
        EmptyComponent* TooDeep = CreateEmpty(run, &Factory);
        if (TooDeep != NULL)
        {
            TEST_CHECK_ERROR(run, TextComponent_AddSubComponent(Deepest, EmptyComponent_AsComponent(TooDeep)),
                ErrorCode_InvalidOperation);
            ReturnTree(run, &Factory, EmptyComponent_AsComponent(TooDeep));
        }
    }

    // A subtree is checked as a whole: 3 levels fit below depth 61 but not below depth 62.
    TextComponent* SubtreeDeepest = NULL;
    TextComponent* Subtree = BuildChain(run, &Factory, 3U, &SubtreeDeepest);
    if ((Root != NULL) && (Subtree != NULL))
    {
        TextComponent* Depth62 = Root;
        for (size_t Level = 1; Level < 62U; Level++)
        {
            Depth62 = TextComponent_GetSubComponentAt(Depth62, 0U);
        }
        TEST_CHECK(run, TextComponent_GetDepth(Depth62) == 62U);
        TEST_CHECK_ERROR(run, TextComponent_AddSubComponent(Depth62, Subtree), ErrorCode_InvalidOperation);
        TEST_CHECK_SUCCESS(run, TextComponent_AddSubComponent(TextComponent_GetParent(Depth62), Subtree));
        Subtree = NULL;
    }
    ReturnTree(run, &Factory, Subtree);
    ReturnTree(run, &Factory, Root);
    TEST_CHECK(run, TextComponentFactory_GetLiveComponentCount(&Factory) == 0U);
    TEST_CHECK_SUCCESS(run, TextComponentFactory_Deconstruct(&Factory));
}

static void DetachAndDestroyKeepHeightsAndOwnership(TestRun* run)
{
    TextComponentFactory Factory;
    if (!TEST_CHECK_SUCCESS(run, TextComponentFactory_Construct(&Factory)))
    {
        return;
    }
    TextComponent* Deepest = NULL;
    TextComponent* Root = BuildChain(run, &Factory, 5U, &Deepest);
    if (Root != NULL)
    {
        TextComponent* Level2 = TextComponent_GetSubComponentAt(Root, 0U);
        TextComponent* Level3 = TextComponent_GetSubComponentAt(Level2, 0U);
        TEST_CHECK_SUCCESS(run, TextComponent_Detach(Level3));
        TEST_CHECK(run, TextComponent_GetParent(Level3) == NULL);
        TEST_CHECK(run, TextComponent_GetHeight(Root) == 2U);
        TEST_CHECK(run, TextComponent_GetHeight(Level3) == 3U);

        // A detached subtree belongs to the caller; returning it while attached is refused.
        TEST_CHECK_ERROR(run, TextComponentFactory_ReturnTree(&Factory, Deepest), ErrorCode_InvalidOperation);
        ReturnTree(run, &Factory, Level3);
        TEST_CHECK(run, TextComponentFactory_GetLiveComponentCount(&Factory) == 2U);

        TEST_CHECK(run, TestText_AddString(run, &Factory, Root, "a") != NULL);
        TEST_CHECK(run, TestText_AddString(run, &Factory, Root, "b") != NULL);
        TEST_CHECK_SUCCESS(run, TextComponent_DestroySubComponentAt(Root, 0U));
        TEST_CHECK(run, TextComponent_GetSubComponentCount(Root) == 2U);
        TEST_CHECK(run, TextComponent_GetHeight(Root) == 2U);

        TextComponent* Detached = NULL;
        TEST_CHECK_SUCCESS(run, TextComponent_DetachSubComponentAt(Root, 1U, &Detached));
        TEST_CHECK(run, (Detached != NULL) && (TextComponent_GetParent(Detached) == NULL));
        ReturnTree(run, &Factory, Detached);
        TEST_CHECK_ERROR(run, TextComponent_DetachSubComponentAt(Root, 5U, &Detached), ErrorCode_IndexOutOfBounds);

        TEST_CHECK_SUCCESS(run, TextComponent_ClearSubComponents(Root));
        TEST_CHECK(run, TextComponent_GetSubComponentCount(Root) == 0U);
        TEST_CHECK(run, TextComponent_GetHeight(Root) == 1U);
        TEST_CHECK(run, TextComponentFactory_GetLiveComponentCount(&Factory) == 1U);
    }
    ReturnTree(run, &Factory, Root);
    TEST_CHECK(run, TextComponentFactory_GetLiveComponentCount(&Factory) == 0U);
    TEST_CHECK_SUCCESS(run, TextComponentFactory_Deconstruct(&Factory));
}


// Adds a chain of levelCount components under parent; returns false (the chain returned) when it cannot.
static bool AddChain(TestRun* run, TextComponentFactory* factory, TextComponent* parent, size_t levelCount, TextComponent** outChain)
{
    TextComponent* Deepest = NULL;
    TextComponent* Chain = BuildChain(run, factory, levelCount, &Deepest);
    *outChain = NULL;
    if ((Chain == NULL) || !TEST_CHECK_SUCCESS(run, TextComponent_AddSubComponent(parent, Chain)))
    {
        ReturnTree(run, factory, Chain);
        return false;
    }
    *outChain = Chain;
    return true;
}

// Heights stay right in wide trees, and keeping them is cheap: an add or a removal only walks up while a height
// changes, so 20000 siblings take no time (a rescan of the siblings on every add would take seconds).
static void WideTreesKeepTheirHeights(TestRun* run)
{
    TextComponentFactory Factory;
    if (!TEST_CHECK_SUCCESS(run, TextComponentFactory_Construct(&Factory)))
    {
        return;
    }
    EmptyComponent* Root = CreateEmpty(run, &Factory);
    TextComponent* RootBase = (Root != NULL) ? EmptyComponent_AsComponent(Root) : NULL;
    const size_t LeafCount = 20000U;
    bool IsBuilt = (RootBase != NULL);
    for (size_t Index = 0U; IsBuilt && (Index < LeafCount); Index++)
    {
        EmptyComponent* Leaf = CreateEmpty(run, &Factory);
        IsBuilt = (Leaf != NULL) && TEST_CHECK_SUCCESS(run, TextComponent_AddSubComponent(RootBase, EmptyComponent_AsComponent(Leaf)));
        if (!IsBuilt && (Leaf != NULL))
        {
            ReturnTree(run, &Factory, EmptyComponent_AsComponent(Leaf));
        }
    }
    if (IsBuilt)
    {
        TEST_CHECK(run, TextComponent_GetHeight(RootBase) == 2U);

        // Two leaves get a 2-level chain each: both are 3 high, the root 4. Removing one chain keeps the root's height
        // (the other is as deep); removing the second lowers it back to 2.
        TextComponent* First = TextComponent_GetSubComponentAt(RootBase, 10U);
        TextComponent* Second = TextComponent_GetSubComponentAt(RootBase, LeafCount - 10U);
        TextComponent* FirstChain = NULL;
        TextComponent* SecondChain = NULL;
        if (AddChain(run, &Factory, First, 2U, &FirstChain) && AddChain(run, &Factory, Second, 2U, &SecondChain))
        {
            TEST_CHECK(run, (TextComponent_GetHeight(First) == 3U) && (TextComponent_GetHeight(RootBase) == 4U));
            TEST_CHECK_SUCCESS(run, TextComponent_DestroySubComponentAt(RootBase, 0U));
            TEST_CHECK(run, TextComponent_GetHeight(RootBase) == 4U);
            TEST_CHECK_SUCCESS(run, TextComponent_Detach(FirstChain));
            ReturnTree(run, &Factory, FirstChain);
            TEST_CHECK(run, (TextComponent_GetHeight(First) == 1U) && (TextComponent_GetHeight(RootBase) == 4U));

            // A deeper chain below the 3-high leaf raises everything above it; taking it away restores the heights.
            TextComponent* Deeper = NULL;
            if (AddChain(run, &Factory, SecondChain, 3U, &Deeper))
            {
                TEST_CHECK(run, (TextComponent_GetHeight(Second) == 5U) && (TextComponent_GetHeight(RootBase) == 6U));
                TEST_CHECK_SUCCESS(run, TextComponent_DestroySubComponentAt(SecondChain, TextComponent_GetSubComponentCount(SecondChain) - 1U));
                TEST_CHECK(run, (TextComponent_GetHeight(Second) == 3U) && (TextComponent_GetHeight(RootBase) == 4U));
            }
            TEST_CHECK_SUCCESS(run, TextComponent_ClearSubComponents(Second));
            TEST_CHECK(run, (TextComponent_GetHeight(Second) == 1U) && (TextComponent_GetHeight(RootBase) == 2U));
        }
        TEST_CHECK_SUCCESS(run, TextComponent_ClearSubComponents(RootBase));
        TEST_CHECK(run, TextComponent_GetHeight(RootBase) == 1U);
    }
    ReturnTree(run, &Factory, RootBase);
    TEST_CHECK(run, TextComponentFactory_GetLiveComponentCount(&Factory) == 0U);
    TEST_CHECK_SUCCESS(run, TextComponentFactory_Deconstruct(&Factory));
}

// Tests: owned strings and validation.
static void StringsAreCopied(TestRun* run)
{
    TextComponentFactory Factory;
    if (!TEST_CHECK_SUCCESS(run, TextComponentFactory_Construct(&Factory)))
    {
        return;
    }
    unsigned char Source[] = u8"original";
    StringComponent* String = NULL;
    if (TEST_CHECK_SUCCESS(run, TextComponentFactory_CreateString(&Factory, Source, &String)))
    {
        Source[0] = (unsigned char)'X';
        TEST_CHECK(run, TextEquals(StringComponent_GetText(String), "original"));

        // A suffix of the component's own text.
        TEST_CHECK_SUCCESS(run, StringComponent_SetText(String, StringComponent_GetText(String) + 4));
        TEST_CHECK(run, TextEquals(StringComponent_GetText(String), "inal"));
        TEST_CHECK_SUCCESS(run, StringComponent_SetText(String, StringComponent_GetText(String)));
        TEST_CHECK(run, TextEquals(StringComponent_GetText(String), "inal"));
        TEST_CHECK_SUCCESS(run, StringComponent_SetText(String, NULL));
        TEST_CHECK(run, TextEquals(StringComponent_GetText(String), ""));

        TEST_CHECK_SUCCESS(run, StringComponent_SetFontName(String, (const unsigned char*)u8"noto_sans"));
        TEST_CHECK(run, TextEquals(StringComponent_GetFontName(String), "noto_sans"));
        TEST_CHECK_SUCCESS(run, StringComponent_SetFontName(String, NULL));
        TEST_CHECK(run, StringComponent_GetFontName(String) == NULL);
        ReturnTree(run, &Factory, StringComponent_AsComponent(String));
    }
    TEST_CHECK_SUCCESS(run, TextComponentFactory_Deconstruct(&Factory));
}

static void NumberShorthandsOwnTheirText(TestRun* run)
{
    TextComponentFactory Factory;
    if (!TEST_CHECK_SUCCESS(run, TextComponentFactory_Construct(&Factory)))
    {
        return;
    }
    StringComponent* Five = NULL;
    StringComponent* Seven = NULL;
    StringComponent* Hex = NULL;
    StringComponent* Half = NULL;
    StringComponent* Letter = NULL;
    TEST_CHECK_SUCCESS(run, TextComponentFactory_CreateInt64(&Factory, 5, &Five));
    TEST_CHECK_SUCCESS(run, TextComponentFactory_CreateInt64(&Factory, 7, &Seven));
    TEST_CHECK_SUCCESS(run, TextComponentFactory_CreateUInt64Formatted(&Factory, 255U, NUMBER_BASE_16, true, &Hex));
    TEST_CHECK_SUCCESS(run, TextComponentFactory_CreateDouble(&Factory, 0.5, &Half));
    TEST_CHECK_SUCCESS(run, TextComponentFactory_CreateCodepoint(&Factory, (CodePoint)0x0101, &Letter));
    if ((Five != NULL) && (Seven != NULL) && (Hex != NULL) && (Half != NULL) && (Letter != NULL))
    {
        // Each owns its text: creating the second number does not extend the first one.
        TEST_CHECK(run, TextEquals(StringComponent_GetText(Five), "5"));
        TEST_CHECK(run, TextEquals(StringComponent_GetText(Seven), "7"));
        TEST_CHECK(run, (strcmp((const char*)StringComponent_GetText(Hex), "0xFF") == 0)
            || (strcmp((const char*)StringComponent_GetText(Hex), "0xff") == 0));
        TEST_CHECK(run, TextEquals(StringComponent_GetText(Half), "0.5"));
        TEST_CHECK(run, TextEquals(StringComponent_GetText(Letter), "\xC4\x81"));
    }
    StringComponent* Surrogate = NULL;
    TEST_CHECK_ERROR(run, TextComponentFactory_CreateCodepoint(&Factory, (CodePoint)0xD800, &Surrogate), ErrorCode_InvalidCodePoint);
    TEST_CHECK(run, Surrogate == NULL);
    StringComponent* Created[] = { Five, Seven, Hex, Half, Letter };
    for (size_t Index = 0; Index < (sizeof(Created) / sizeof(Created[0])); Index++)
    {
        ReturnTree(run, &Factory, (Created[Index] != NULL) ? StringComponent_AsComponent(Created[Index]) : NULL);
    }
    TEST_CHECK(run, TextComponentFactory_GetLiveComponentCount(&Factory) == 0U);
    TEST_CHECK_SUCCESS(run, TextComponentFactory_Deconstruct(&Factory));
}

static void SettersRejectInvalidValues(TestRun* run)
{
    TextComponentFactory Factory;
    if (!TEST_CHECK_SUCCESS(run, TextComponentFactory_Construct(&Factory)))
    {
        return;
    }
    const unsigned char InvalidUTF8[] = { 0xC3, 0x28, 0x00 };
    StringComponent* String = NULL;
    SpriteComponent* Sprite = NULL;
    TEST_CHECK_ERROR(run, TextComponentFactory_CreateString(&Factory, InvalidUTF8, &String), ErrorCode_InvalidTextEncoding);
    TEST_CHECK(run, TextComponentFactory_GetLiveComponentCount(&Factory) == 0U);
    TEST_CHECK_ERROR(run, TextComponentFactory_CreateSprite(&Factory, (const unsigned char*)u8"Coin Spin", &Sprite),
        ErrorCode_ArgumentOutOfRange);

    if (TEST_CHECK_SUCCESS(run, TextComponentFactory_CreateString(&Factory, (const unsigned char*)u8"x", &String))
        && TEST_CHECK_SUCCESS(run, TextComponentFactory_CreateSprite(&Factory, (const unsigned char*)u8"coin_spin", &Sprite)))
    {
        RenderColor TooBright = RenderColor_White();
        TooBright.Brightness = 2.0f;
        TEST_CHECK_ERROR(run, StringComponent_SetText(String, InvalidUTF8), ErrorCode_InvalidTextEncoding);
        TEST_CHECK(run, TextEquals(StringComponent_GetText(String), "x"));
        TEST_CHECK_ERROR(run, StringComponent_SetFontName(String, (const unsigned char*)u8"Fonts/Bold"), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, StringComponent_SetSize(String, -0.1f), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, StringComponent_SetSize(String, TEXT_COMPONENT_SIZE_MAX * 2.0f), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, StringComponent_SetSize(String, (float)NAN), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, StringComponent_SetColor(String, TooBright), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, StringComponent_SetShadowColorCustom(String, TooBright), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, StringComponent_SetShadowOffsetCustom(String, (Vector2){ 20.0f, 0.0f }), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, StringComponent_SetShadowOffsetCustom(String, (Vector2){ 0.0f, (float)NAN }),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, StringComponent_GetShadowOffsetType(String) == TextShadowOffsetType_Default);
        TEST_CHECK_ERROR(run, StringComponent_SetSpacing(String, -2.0f), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, SpriteComponent_SetSize(Sprite, (Vector2){ -1.0f, 1.0f }), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, SpriteComponent_SetColor(Sprite, TooBright), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, StringComponent_GetSize(String) == STRING_COMPONENT_DEFAULT_SIZE);
    }
    ReturnTree(run, &Factory, (String != NULL) ? StringComponent_AsComponent(String) : NULL);
    ReturnTree(run, &Factory, (Sprite != NULL) ? SpriteComponent_AsComponent(Sprite) : NULL);
    TEST_CHECK(run, TextComponentFactory_GetLiveComponentCount(&Factory) == 0U);
    TEST_CHECK_SUCCESS(run, TextComponentFactory_Deconstruct(&Factory));
}

static void RevisionsReachTheRoot(TestRun* run)
{
    TextComponentFactory Factory;
    if (!TEST_CHECK_SUCCESS(run, TextComponentFactory_Construct(&Factory)))
    {
        return;
    }
    TextComponent* Deepest = NULL;
    TextComponent* Root = BuildChain(run, &Factory, 3U, &Deepest);
    StringComponent* Leaf = (Deepest != NULL) ? TestText_AddString(run, &Factory, Deepest, "leaf") : NULL;
    if ((Root != NULL) && (Leaf != NULL))
    {
        uint64_t Before = TextComponent_GetRevision(Root);
        TEST_CHECK(run, TextComponent_GetSubComponentCount(Root) == 1U); // Reading changes nothing.
        TEST_CHECK(run, TextComponent_GetRevision(Root) == Before);

        TEST_CHECK_SUCCESS(run, StringComponent_SetIsUnderlined(Leaf, true));
        uint64_t AfterEdit = TextComponent_GetRevision(Root);
        TEST_CHECK(run, AfterEdit != Before);
        TEST_CHECK(run, TextComponent_GetRevision(StringComponent_AsComponent(Leaf)) == AfterEdit);

        TEST_CHECK_SUCCESS(run, TextComponent_Detach(StringComponent_AsComponent(Leaf)));
        TEST_CHECK(run, TextComponent_GetRevision(Root) != AfterEdit);
        ReturnTree(run, &Factory, StringComponent_AsComponent(Leaf));
    }
    ReturnTree(run, &Factory, Root);
    TEST_CHECK_SUCCESS(run, TextComponentFactory_Deconstruct(&Factory));
}


// Tests: clone, translate, style and plain text.
static TextComponent* BuildMixedTree(TestRun* run, TextComponentFactory* factory)
{
    EmptyComponent* Root = CreateEmpty(run, factory);
    if (Root == NULL)
    {
        return NULL;
    }
    TextComponent* RootBase = EmptyComponent_AsComponent(Root);
    StringComponent* Greeting = TestText_AddString(run, factory, RootBase, "Hello ");
    TranslateComponent* Title = NULL;
    SpriteComponent* Coin = NULL;
    bool IsBuilt = (Greeting != NULL)
        && TEST_CHECK_SUCCESS(run, StringComponent_SetFontName(Greeting, (const unsigned char*)u8"noto_sans"))
        && TEST_CHECK_SUCCESS(run, StringComponent_SetSize(Greeting, 0.25f))
        && TEST_CHECK_SUCCESS(run, TextComponentFactory_CreateTranslate(factory, (const unsigned char*)u8"menu.title",
            (const unsigned char*)u8"Title", &Title))
        && TEST_CHECK_SUCCESS(run, TextComponent_AddSubComponent(StringComponent_AsComponent(Greeting),
            TranslateComponent_AsComponent(Title)))
        && TEST_CHECK_SUCCESS(run, TextComponentFactory_CreateSprite(factory, (const unsigned char*)u8"coin_spin", &Coin))
        && TEST_CHECK_SUCCESS(run, SpriteComponent_SetSize(Coin, (Vector2){ 0.1f, 0.2f }))
        && TEST_CHECK_SUCCESS(run, TextComponent_AddSubComponent(RootBase, SpriteComponent_AsComponent(Coin)));
    if (!IsBuilt)
    {
        if ((Title != NULL) && (TextComponent_GetParent(TranslateComponent_AsComponent(Title)) == NULL))
        {
            ReturnTree(run, factory, TranslateComponent_AsComponent(Title));
        }
        if ((Coin != NULL) && (TextComponent_GetParent(SpriteComponent_AsComponent(Coin)) == NULL))
        {
            ReturnTree(run, factory, SpriteComponent_AsComponent(Coin));
        }
        ReturnTree(run, factory, RootBase);
        return NULL;
    }
    return RootBase;
}

static void CloneTreeCopiesEverything(TestRun* run)
{
    TextComponentFactory Factory;
    TextComponentFactory Other;
    if (!TEST_CHECK_SUCCESS(run, TextComponentFactory_Construct(&Factory)) || !TEST_CHECK_SUCCESS(run, TextComponentFactory_Construct(&Other)))
    {
        return;
    }
    TextComponent* Source = BuildMixedTree(run, &Factory);
    TextComponent* Clone = NULL;
    if ((Source != NULL) && TEST_CHECK_SUCCESS(run, TextComponentFactory_CloneTree(&Other, Source, &Clone)))
    {
        TEST_CHECK(run, TestText_AreTreesEqual(Source, Clone));
        TEST_CHECK(run, TextComponent_GetFactory(Clone) == &Other);
        TEST_CHECK(run, TextComponentFactory_GetLiveComponentCount(&Other) == TextComponentFactory_GetLiveComponentCount(&Factory));

        StringComponent* ClonedGreeting = TextComponent_AsStringComponent(TextComponent_GetSubComponentAt(Clone, 0U));
        TEST_CHECK_SUCCESS(run, StringComponent_SetText(ClonedGreeting, (const unsigned char*)u8"Bye "));
        TEST_CHECK(run, !TestText_AreTreesEqual(Source, Clone));
        TEST_CHECK(run, TextEquals(StringComponent_GetText(TextComponent_AsStringComponent(TextComponent_GetSubComponentAt(Source, 0U))),
            "Hello "));
    }
    ReturnTree(run, &Other, Clone);
    ReturnTree(run, &Factory, Source);
    TEST_CHECK_SUCCESS(run, TextComponentFactory_Deconstruct(&Factory));
    TEST_CHECK_SUCCESS(run, TextComponentFactory_Deconstruct(&Other));
}

static void TranslateComponentsNeedAValidKey(TestRun* run)
{
    TextComponentFactory Factory;
    if (!TEST_CHECK_SUCCESS(run, TextComponentFactory_Construct(&Factory)))
    {
        return;
    }
    TranslateComponent* Translate = NULL;
    TEST_CHECK_ERROR(run, TextComponentFactory_CreateTranslate(&Factory, (const unsigned char*)u8"Menu Title", NULL, &Translate),
        ErrorCode_ArgumentOutOfRange);
    TEST_CHECK(run, TextComponentFactory_GetLiveComponentCount(&Factory) == 0U);
    if (TEST_CHECK_SUCCESS(run, TextComponentFactory_CreateTranslate(&Factory, (const unsigned char*)u8"menu.title", NULL, &Translate)))
    {
        TEST_CHECK(run, TextEquals(TranslateComponent_GetKey(Translate), "menu.title"));
        TEST_CHECK(run, TextEquals(TranslateComponent_GetFallback(Translate), ""));
        TEST_CHECK_SUCCESS(run, TranslateComponent_SetFallback(Translate, (const unsigned char*)u8"Title"));
        TEST_CHECK(run, TextEquals(StringComponent_GetText(TranslateComponent_AsString(Translate)), "Title"));
        TEST_CHECK_ERROR(run, TranslateComponent_SetKey(Translate, (const unsigned char*)u8""), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, TextEquals(TranslateComponent_GetKey(Translate), "menu.title"));
        TEST_CHECK(run, TextComponent_AsStringComponent(TranslateComponent_AsComponent(Translate)) != NULL);
        ReturnTree(run, &Factory, TranslateComponent_AsComponent(Translate));
    }
    TEST_CHECK_SUCCESS(run, TextComponentFactory_Deconstruct(&Factory));
}

// Without an offset of its own a string uses its font's; a custom offset wins until it is set back to the default.
static void ShadowOffsetsComeFromTheFontUnlessSet(TestRun* run)
{
    TextComponentFactory Factory;
    if (!TEST_CHECK_SUCCESS(run, TextComponentFactory_Construct(&Factory)))
    {
        return;
    }
    StringComponent* String = NULL;
    if (TEST_CHECK_SUCCESS(run, TextComponentFactory_CreateString(&Factory, (const unsigned char*)u8"x", &String)))
    {
        Vector2 FontOffset = { -0.05f, 0.05f };
        Vector2 Resolved = StringComponent_ResolveShadowOffset(String, FontOffset);
        TEST_CHECK(run, (Resolved.x == -0.05f) && (Resolved.y == 0.05f));

        uint64_t Revision = TextComponent_GetRevision(StringComponent_AsComponent(String));
        TEST_CHECK_SUCCESS(run, StringComponent_SetShadowOffsetCustom(String, (Vector2){ 0.2f, 0.3f }));
        TEST_CHECK(run, StringComponent_GetShadowOffsetType(String) == TextShadowOffsetType_Custom);
        TEST_CHECK(run, TextComponent_GetRevision(StringComponent_AsComponent(String)) != Revision);
        Resolved = StringComponent_ResolveShadowOffset(String, FontOffset);
        TEST_CHECK(run, (Resolved.x == 0.2f) && (Resolved.y == 0.3f));

        Revision = TextComponent_GetRevision(StringComponent_AsComponent(String));
        TEST_CHECK_SUCCESS(run, StringComponent_SetShadowOffsetDefault(String));
        TEST_CHECK(run, StringComponent_GetShadowOffsetType(String) == TextShadowOffsetType_Default);
        TEST_CHECK(run, TextComponent_GetRevision(StringComponent_AsComponent(String)) != Revision);
        Resolved = StringComponent_ResolveShadowOffset(String, FontOffset);
        TEST_CHECK(run, (Resolved.x == -0.05f) && (Resolved.y == 0.05f));

        // A clone keeps where the offset comes from.
        TextComponent* Copy = NULL;
        if (TEST_CHECK_SUCCESS(run, StringComponent_SetShadowOffsetCustom(String, (Vector2){ -0.1f, 0.1f }))
            && TEST_CHECK_SUCCESS(run, TextComponentFactory_CloneTree(&Factory, StringComponent_AsComponent(String), &Copy)))
        {
            const StringComponent* Cloned = TextComponent_AsConstStringComponent(Copy);
            TEST_CHECK(run, StringComponent_GetShadowOffsetType(Cloned) == TextShadowOffsetType_Custom);
            TEST_CHECK(run, StringComponent_GetShadowOffset(Cloned).x == -0.1f);
            ReturnTree(run, &Factory, Copy);
        }
        ReturnTree(run, &Factory, StringComponent_AsComponent(String));
    }
    TEST_CHECK(run, TextComponentFactory_GetLiveComponentCount(&Factory) == 0U);
    TEST_CHECK_SUCCESS(run, TextComponentFactory_Deconstruct(&Factory));
}

static void TextStyleAppliesByType(TestRun* run)
{
    TextComponentFactory Factory;
    if (!TEST_CHECK_SUCCESS(run, TextComponentFactory_Construct(&Factory)))
    {
        return;
    }
    TextStyle Style;
    TextStyle_Construct(&Style);
    TextStyle_SetFontName(&Style, (const unsigned char*)u8"noto_sans");
    TextStyle_SetStringSize(&Style, 0.3f);
    TextStyle_SetSpriteSize(&Style, (Vector2){ 0.2f, 0.2f });
    TextStyle_SetIsUnderlined(&Style, true);

    TextComponent* Root = BuildMixedTree(run, &Factory);
    if (Root != NULL)
    {
        TextComponent* Greeting = TextComponent_GetSubComponentAt(Root, 0U);
        TextComponent* Title = TextComponent_GetSubComponentAt(Greeting, 0U);
        TextComponent* Coin = TextComponent_GetSubComponentAt(Root, 1U);
        TEST_CHECK_SUCCESS(run, TextStyle_ApplyToComponent(&Style, Title));
        TEST_CHECK_SUCCESS(run, TextStyle_ApplyToComponent(&Style, Coin));
        TEST_CHECK_SUCCESS(run, TextStyle_ApplyToComponent(&Style, Root));
        TEST_CHECK(run, TextEquals(StringComponent_GetFontName(TextComponent_AsStringComponent(Title)), "noto_sans"));
        TEST_CHECK(run, StringComponent_GetSize(TextComponent_AsStringComponent(Title)) == 0.3f);
        TEST_CHECK(run, StringComponent_IsUnderlined(TextComponent_AsStringComponent(Title)));
        TEST_CHECK(run, SpriteComponent_GetSize(TextComponent_AsSpriteComponent(Coin)).x == 0.2f);

        // The shadow offset directive sets a custom offset or goes back to the font's.
        StringComponent* TitleString = TextComponent_AsStringComponent(Title);
        TextStyle OffsetStyle;
        TextStyle_Construct(&OffsetStyle);
        TextStyle_SetShadowOffsetCustom(&OffsetStyle, (Vector2){ 0.5f, 0.5f });
        TEST_CHECK_SUCCESS(run, TextStyle_ApplyToComponent(&OffsetStyle, Title));
        TEST_CHECK(run, (StringComponent_GetShadowOffsetType(TitleString) == TextShadowOffsetType_Custom)
            && (StringComponent_GetShadowOffset(TitleString).x == 0.5f));
        TextStyle_SetShadowOffsetDefault(&OffsetStyle);
        TEST_CHECK_SUCCESS(run, TextStyle_ApplyToComponent(&OffsetStyle, Title));
        TEST_CHECK(run, StringComponent_GetShadowOffsetType(TitleString) == TextShadowOffsetType_Default);
        TextStyle_Deconstruct(&OffsetStyle);

        TextStyle_SetFontName(&Style, (const unsigned char*)u8"Not A Name");
        TEST_CHECK_ERROR(run, TextStyle_ApplyToComponent(&Style, Greeting), ErrorCode_ArgumentOutOfRange);
    }
    TextStyle_Deconstruct(&Style);
    ReturnTree(run, &Factory, Root);
    TEST_CHECK_SUCCESS(run, TextComponentFactory_Deconstruct(&Factory));
}

static void PlainTextShowsStringsAndTranslations(TestRun* run)
{
    TextComponentFactory Factory;
    if (!TEST_CHECK_SUCCESS(run, TextComponentFactory_Construct(&Factory)))
    {
        return;
    }
    TextComponent* Root = BuildMixedTree(run, &Factory);
    TranslateComponent* Untranslated = NULL;
    if ((Root != NULL)
        && TEST_CHECK_SUCCESS(run, TextComponentFactory_CreateTranslate(&Factory, (const unsigned char*)u8"menu.quit", NULL, &Untranslated)))
    {
        if (TEST_CHECK_SUCCESS(run, TextComponent_AddSubComponent(Root, TranslateComponent_AsComponent(Untranslated))))
        {
            GenericBuffer Text;
            GenericBuffer_AllocateVariable(&Text, 0U, sizeof(unsigned char));
            TEST_CHECK_SUCCESS(run, TextComponentText_Serialize(Root, &Text));
            TEST_CHECK(run, GenericBuffer_NullTerminate(&Text) && TextEquals(Text._data, "Hello Titlemenu.quit"));
            Memory_Free(Text._data);
        }
        else
        {
            ReturnTree(run, &Factory, TranslateComponent_AsComponent(Untranslated));
        }
    }
    ReturnTree(run, &Factory, Root);
    TEST_CHECK_SUCCESS(run, TextComponentFactory_Deconstruct(&Factory));
}


// Public functions.
void TextComponentTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "created_component_is_a_root_with_defaults", CreatedComponentIsARootWithDefaults },
        { "add_rejects_a_child_that_has_a_parent", AddRejectsAChildThatHasAParent },
        { "add_rejects_cycles_and_other_factories", AddRejectsCyclesAndOtherFactories },
        { "depth_limit_is_enforced", DepthLimitIsEnforced },
        { "detach_and_destroy_keep_heights_and_ownership", DetachAndDestroyKeepHeightsAndOwnership },
        { "wide_trees_keep_their_heights", WideTreesKeepTheirHeights },
        { "strings_are_copied", StringsAreCopied },
        { "number_shorthands_own_their_text", NumberShorthandsOwnTheirText },
        { "setters_reject_invalid_values", SettersRejectInvalidValues },
        { "revisions_reach_the_root", RevisionsReachTheRoot },
        { "clone_tree_copies_everything", CloneTreeCopiesEverything },
        { "translate_components_need_a_valid_key", TranslateComponentsNeedAValidKey },
        { "shadow_offsets_come_from_the_font_unless_set", ShadowOffsetsComeFromTheFontUnlessSet },
        { "text_style_applies_by_type", TextStyleAppliesByType },
        { "plain_text_shows_strings_and_translations", PlainTextShowsStringsAndTranslations },
    };
    TestRun_RunSuite(run, "TextComponent", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
