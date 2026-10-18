#include "TestSuites.h"
#include "TestAssets.h"
#include "TestText.h"
#include "TextDocument.h"
#include "TextComponentFactory.h"
#include "ProgramTime.h"
#include <string.h>


// Types.
/* A factory, an asset manager with test assets, and a document using both. */
typedef struct DocumentFixtureStruct
{
    TestAssetFixture Assets;
    TextComponentFactory Factory;
    TextDocument Document;
    bool IsReady;
} DocumentFixture;


// Static functions.
static bool DocumentFixture_Construct(TestRun* run, DocumentFixture* self, bool hasAssets)
{
    Memory_Zero(self, sizeof(*self));
    if (!TestAssetFixture_Construct(run, &self->Assets) || !TEST_CHECK_SUCCESS(run, TextComponentFactory_Construct(&self->Factory)))
    {
        return false;
    }
    self->IsReady = TEST_CHECK_SUCCESS(run, TextDocument_Construct(&self->Document, &self->Factory,
        hasAssets ? self->Assets.Manager : NULL, NULL));
    return self->IsReady;
}

static void DocumentFixture_Deconstruct(TestRun* run, DocumentFixture* self)
{
    if (self->IsReady)
    {
        TEST_CHECK_SUCCESS(run, TextDocument_Deconstruct(&self->Document));
    }
    TEST_CHECK(run, TextComponentFactory_GetLiveComponentCount(&self->Factory) == 0U);
    TEST_CHECK_SUCCESS(run, TextComponentFactory_Deconstruct(&self->Factory));
    TestAssetFixture_Deconstruct(run, &self->Assets);
}

static TextComponent* CreateString(TestRun* run, TextComponentFactory* factory, const char* text)
{
    StringComponent* String = NULL;
    TEST_CHECK_SUCCESS(run, TextComponentFactory_CreateString(factory, (const unsigned char*)text, &String));
    return (String != NULL) ? StringComponent_AsComponent(String) : NULL;
}


// Tests.
static void SetRootTakesOwnershipAndReturnsTheOldTree(TestRun* run)
{
    DocumentFixture Fixture;
    if (DocumentFixture_Construct(run, &Fixture, true))
    {
        TextComponent* First = CreateString(run, &Fixture.Factory, "first");
        TextComponent* Second = CreateString(run, &Fixture.Factory, "second");
        uint64_t InitialRevision = TextDocument_GetRevision(&Fixture.Document);
        if ((First != NULL) && (Second != NULL))
        {
            TEST_CHECK_SUCCESS(run, TextDocument_SetRoot(&Fixture.Document, First));
            TEST_CHECK(run, TextDocument_GetRoot(&Fixture.Document) == First);
            TEST_CHECK(run, TextDocument_GetRevision(&Fixture.Document) != InitialRevision);
            TEST_CHECK_SUCCESS(run, TextDocument_SetRoot(&Fixture.Document, First));
            TEST_CHECK_SUCCESS(run, TextDocument_SetRoot(&Fixture.Document, Second));
            TEST_CHECK(run, TextComponentFactory_GetLiveComponentCount(&Fixture.Factory) == 1U);

            TextComponent* Taken = NULL;
            TEST_CHECK_SUCCESS(run, TextDocument_TakeRoot(&Fixture.Document, &Taken));
            TEST_CHECK(run, (Taken == Second) && (TextDocument_GetRoot(&Fixture.Document) == NULL));
            TEST_CHECK_SUCCESS(run, TextDocument_SetRoot(&Fixture.Document, Taken));
        }
    }
    DocumentFixture_Deconstruct(run, &Fixture);
}

static void SetRootRefusesAttachedAndForeignComponents(TestRun* run)
{
    DocumentFixture Fixture;
    TextComponentFactory Other;
    if (DocumentFixture_Construct(run, &Fixture, true) && TEST_CHECK_SUCCESS(run, TextComponentFactory_Construct(&Other)))
    {
        TextComponent* Parent = CreateString(run, &Fixture.Factory, "parent");
        StringComponent* Child = (Parent != NULL) ? TestText_AddString(run, &Fixture.Factory, Parent, "child") : NULL;
        TextComponent* Foreign = CreateString(run, &Other, "foreign");
        if ((Child != NULL) && (Foreign != NULL))
        {
            TEST_CHECK_ERROR(run, TextDocument_SetRoot(&Fixture.Document, StringComponent_AsComponent(Child)), ErrorCode_InvalidOperation);
            TEST_CHECK_ERROR(run, TextDocument_SetRoot(&Fixture.Document, Foreign), ErrorCode_IllegalArgument);
            TEST_CHECK(run, TextDocument_GetRoot(&Fixture.Document) == NULL);
        }
        if (Parent != NULL)
        {
            TEST_CHECK_SUCCESS(run, TextComponentFactory_ReturnTree(&Fixture.Factory, Parent));
        }
        if (Foreign != NULL)
        {
            TEST_CHECK_SUCCESS(run, TextComponentFactory_ReturnTree(&Other, Foreign));
        }
        TEST_CHECK_SUCCESS(run, TextComponentFactory_Deconstruct(&Other));
    }
    DocumentFixture_Deconstruct(run, &Fixture);
}

// While a document holds a root, the tree API refuses to give it away (to another document, to a parent, or back to
// the factory); it stays editable in place, and taking it out of the document releases it.
static void HeldRootsCannotBeGivenAway(TestRun* run)
{
    DocumentFixture Fixture;
    TextDocument Other;
    bool IsOtherConstructed = false;
    if (DocumentFixture_Construct(run, &Fixture, true))
    {
        IsOtherConstructed = TEST_CHECK_SUCCESS(run, TextDocument_Construct(&Other, &Fixture.Factory, Fixture.Assets.Manager, NULL));
    }
    if (IsOtherConstructed)
    {
        TextComponent* Root = CreateString(run, &Fixture.Factory, "held");
        TextComponent* Parent = CreateString(run, &Fixture.Factory, "parent");
        if ((Root != NULL) && (Parent != NULL) && TEST_CHECK_SUCCESS(run, TextDocument_SetRoot(&Fixture.Document, Root)))
        {
            TEST_CHECK(run, TextComponent_IsHeld(Root));
            TEST_CHECK_ERROR(run, TextDocument_SetRoot(&Other, Root), ErrorCode_InvalidOperation);
            TEST_CHECK_ERROR(run, TextComponent_AddSubComponent(Parent, Root), ErrorCode_InvalidOperation);
            TEST_CHECK_ERROR(run, TextComponent_InsertSubComponent(Parent, 0U, Root), ErrorCode_InvalidOperation);
            TEST_CHECK_ERROR(run, TextComponentFactory_ReturnTree(&Fixture.Factory, Root), ErrorCode_InvalidOperation);
            TEST_CHECK(run, (TextDocument_GetRoot(&Fixture.Document) == Root) && (TextDocument_GetRoot(&Other) == NULL));
            TEST_CHECK(run, (TextComponent_GetParent(Root) == NULL) && (TextComponent_GetSubComponentCount(Parent) == 0U));

            TEST_CHECK_SUCCESS(run, StringComponent_SetText(TextComponent_AsStringComponent(Root), (const unsigned char*)u8"edited"));
            TEST_CHECK_SUCCESS(run, TextDocument_SetRoot(&Fixture.Document, Root));

            // A clone is a new, free tree.
            TextComponent* Copy = NULL;
            if (TEST_CHECK_SUCCESS(run, TextComponentFactory_CloneTree(&Fixture.Factory, Root, &Copy)))
            {
                TEST_CHECK(run, !TextComponent_IsHeld(Copy));
                TEST_CHECK_SUCCESS(run, TextDocument_SetRoot(&Other, Copy));
            }

            TextComponent* Taken = NULL;
            TEST_CHECK_SUCCESS(run, TextDocument_TakeRoot(&Fixture.Document, &Taken));
            TEST_CHECK(run, (Taken == Root) && !TextComponent_IsHeld(Root));
            if (TEST_CHECK_SUCCESS(run, TextComponent_AddSubComponent(Parent, Root)))
            {
                Root = NULL;
            }
        }
        else if ((Root != NULL) && !TextComponent_IsHeld(Root))
        {
            TEST_CHECK_SUCCESS(run, TextComponentFactory_ReturnTree(&Fixture.Factory, Root));
            Root = NULL;
        }
        if ((Root != NULL) && !TextComponent_IsHeld(Root))
        {
            TEST_CHECK_SUCCESS(run, TextComponentFactory_ReturnTree(&Fixture.Factory, Root));
        }
        if (Parent != NULL)
        {
            TEST_CHECK_SUCCESS(run, TextComponentFactory_ReturnTree(&Fixture.Factory, Parent));
        }
        TEST_CHECK_SUCCESS(run, TextDocument_Deconstruct(&Other));
    }
    DocumentFixture_Deconstruct(run, &Fixture);
}

static void FontsBindOnceAndFallBack(TestRun* run)
{
    DocumentFixture Fixture;
    TestAssetCounters MonoCounters = { 0 };
    TestAssetCounters WideCounters = { 0 };
    if (DocumentFixture_Construct(run, &Fixture, true)
        && TestAssets_AddFont(run, &Fixture.Assets, "mono", 5, &MonoCounters)
        && TestAssets_AddFont(run, &Fixture.Assets, "wide", 8, &WideCounters))
    {
        const GameFont* First = NULL;
        const GameFont* Again = NULL;
        const GameFont* Missing = NULL;
        const GameFont* Default = NULL;
        TEST_CHECK_SUCCESS(run, TextDocument_ResolveFont(&Fixture.Document, (const unsigned char*)u8"wide", &First));
        TEST_CHECK_SUCCESS(run, TextDocument_ResolveFont(&Fixture.Document, (const unsigned char*)u8"wide", &Again));
        TEST_CHECK(run, (First != NULL) && (First == Again) && (First->_rayFont.glyphs[0].advanceX == 8));
        TEST_CHECK(run, WideCounters.LoadCount == 1U);

        // Without a default font name, a missing font falls back to raylib's built-in font (empty without a window).
        TEST_CHECK_SUCCESS(run, TextDocument_ResolveFont(&Fixture.Document, (const unsigned char*)u8"missing", &Missing));
        TEST_CHECK(run, (Missing != NULL) && !GameFont_HasGlyphs(Missing));

        TEST_CHECK_SUCCESS(run, TextDocument_SetDefaultFontName(&Fixture.Document, (const unsigned char*)u8"mono"));
        TEST_CHECK_SUCCESS(run, TextDocument_ResolveFont(&Fixture.Document, (const unsigned char*)u8"missing", &Missing));
        TEST_CHECK_SUCCESS(run, TextDocument_ResolveFont(&Fixture.Document, NULL, &Default));
        TEST_CHECK(run, (Missing != NULL) && (Missing == Default) && (Default->_rayFont.glyphs[0].advanceX == 5));
        TEST_CHECK(run, MonoCounters.LoadCount == 1U);

        TEST_CHECK_ERROR(run, TextDocument_SetDefaultFontName(&Fixture.Document, (const unsigned char*)u8"Bad Name"),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, strcmp((const char*)TextDocument_GetDefaultFontName(&Fixture.Document), "mono") == 0);
    }
    if (Fixture.IsReady)
    {
        TEST_CHECK_SUCCESS(run, TextDocument_Deconstruct(&Fixture.Document));
        Fixture.IsReady = false;
        // The document was the only holder, so retiring its user unloaded both fonts.
        TEST_CHECK(run, (MonoCounters.UnloadCount == 1U) && (WideCounters.UnloadCount == 1U));
    }
    DocumentFixture_Deconstruct(run, &Fixture);
}

static void OneInstancePerAnimationName(TestRun* run)
{
    DocumentFixture Fixture;
    TestAssetCounters Counters = { 0 };
    if (DocumentFixture_Construct(run, &Fixture, true)
        && TestAssets_AddPlayingSpriteAnimation(run, &Fixture.Assets, "coin_spin", 4U, 10.0, &Counters)
        && TestAssets_AddSpriteAnimation(run, &Fixture.Assets, "still", NULL))
    {
        SpriteAnimationInstance* Coin = NULL;
        SpriteAnimationInstance* CoinAgain = NULL;
        SpriteAnimationInstance* Still = NULL;
        SpriteAnimationInstance* Missing = NULL;
        SpriteAnimationInstance* Unnamed = NULL;
        TEST_CHECK_SUCCESS(run, TextDocument_ResolveAnimation(&Fixture.Document, (const unsigned char*)u8"coin_spin", &Coin));
        TEST_CHECK_SUCCESS(run, TextDocument_ResolveAnimation(&Fixture.Document, (const unsigned char*)u8"coin_spin", &CoinAgain));
        TEST_CHECK_SUCCESS(run, TextDocument_ResolveAnimation(&Fixture.Document, (const unsigned char*)u8"still", &Still));
        TEST_CHECK_SUCCESS(run, TextDocument_ResolveAnimation(&Fixture.Document, (const unsigned char*)u8"missing", &Missing));
        TEST_CHECK_SUCCESS(run, TextDocument_ResolveAnimation(&Fixture.Document, NULL, &Unnamed));
        TEST_CHECK(run, (Coin != NULL) && (Coin == CoinAgain) && (Still != NULL) && (Still != Coin));
        TEST_CHECK(run, (Missing == NULL) && (Unnamed == NULL));
        TEST_CHECK(run, Counters.LoadCount == 1U);

        // One tick of 0.25 s at 10 frames per second advances the shared instance by two frames.
        if (Coin != NULL)
        {
            TEST_CHECK_SUCCESS(run, TextDocument_Update(&Fixture.Document, ProgramTime_Create(0.25, 0.25)));
            TEST_CHECK(run, SpriteAnimationInstance_GetFrameIndex(Coin) == 2U);
        }
    }
    if (Fixture.IsReady)
    {
        TEST_CHECK_SUCCESS(run, TextDocument_Deconstruct(&Fixture.Document));
        Fixture.IsReady = false;
        TEST_CHECK(run, Counters.UnloadCount == 1U);
    }
    DocumentFixture_Deconstruct(run, &Fixture);
}

static void DocumentWithoutAssetsBindsNothing(TestRun* run)
{
    DocumentFixture Fixture;
    TestAssetCounters Counters = { 0 };
    if (DocumentFixture_Construct(run, &Fixture, false) && TestAssets_AddFont(run, &Fixture.Assets, "mono", 5, &Counters))
    {
        const GameFont* Resolved = NULL;
        SpriteAnimationInstance* Instance = NULL;
        TEST_CHECK_SUCCESS(run, TextDocument_ResolveFont(&Fixture.Document, (const unsigned char*)u8"mono", &Resolved));
        TEST_CHECK_SUCCESS(run, TextDocument_ResolveAnimation(&Fixture.Document, (const unsigned char*)u8"coin_spin", &Instance));
        TEST_CHECK(run, (Resolved != NULL) && !GameFont_HasGlyphs(Resolved) && (Instance == NULL));
        TEST_CHECK(run, Counters.LoadCount == 0U);
        TEST_CHECK(run, TextDocument_ResolveTranslation(&Fixture.Document, (const unsigned char*)u8"menu.title") == NULL);
    }
    DocumentFixture_Deconstruct(run, &Fixture);
}

static void DeconstructReturnsTheTree(TestRun* run)
{
    DocumentFixture Fixture;
    if (DocumentFixture_Construct(run, &Fixture, true))
    {
        TextComponent* Root = CreateString(run, &Fixture.Factory, "root");
        if ((Root != NULL) && (TestText_AddString(run, &Fixture.Factory, Root, "child") != NULL))
        {
            TEST_CHECK_SUCCESS(run, TextDocument_SetRoot(&Fixture.Document, Root));
            TEST_CHECK(run, TextComponentFactory_GetLiveComponentCount(&Fixture.Factory) == 2U);
        }
        else if (Root != NULL)
        {
            TEST_CHECK_SUCCESS(run, TextComponentFactory_ReturnTree(&Fixture.Factory, Root));
        }
    }
    // The fixture checks that the factory has no live components left after the document is deconstructed.
    DocumentFixture_Deconstruct(run, &Fixture);
}


// Public functions.
void TextDocumentTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "set_root_takes_ownership_and_returns_the_old_tree", SetRootTakesOwnershipAndReturnsTheOldTree },
        { "set_root_refuses_attached_and_foreign_components", SetRootRefusesAttachedAndForeignComponents },
        { "held_roots_cannot_be_given_away", HeldRootsCannotBeGivenAway },
        { "fonts_bind_once_and_fall_back", FontsBindOnceAndFallBack },
        { "one_instance_per_animation_name", OneInstancePerAnimationName },
        { "document_without_assets_binds_nothing", DocumentWithoutAssetsBindsNothing },
        { "deconstruct_returns_the_tree", DeconstructReturnsTheTree },
    };
    TestRun_RunSuite(run, "TextDocument", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
