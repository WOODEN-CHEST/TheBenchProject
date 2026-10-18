#include "TestSuites.h"
#include "TestAssets.h"
#include "TestText.h"
#include "LabelWidget.h"
#include "UIScreen.h"
#include "UIWidget.h"
#include "UIWidgetFactory.h"
#include "TextComponentFactory.h"
#include "ProgramTime.h"
#include <math.h>


// Types.
/* A screen with the label type, a component factory and test assets (a 5 px monospace font named "mono"). */
typedef struct LabelFixtureStruct
{
    TestAssetFixture Assets;
    TextComponentFactory Components;
    UIWidgetFactory Widgets;
    UIScreen Screen;
    uint64_t LabelType;
    bool IsScreenConstructed;
    bool IsReady;
} LabelFixture;


// Static functions.
static bool IsNear(float value, float expected)
{
    return fabsf(value - expected) < 1e-5f;
}

static bool LabelFixture_Construct(TestRun* run, LabelFixture* self)
{
    Memory_Zero(self, sizeof(*self));
    bool IsBuilt = TestAssetFixture_Construct(run, &self->Assets)
        && TestAssets_AddFont(run, &self->Assets, "mono", 5, NULL)
        && TEST_CHECK_SUCCESS(run, TextComponentFactory_Construct(&self->Components))
        && TEST_CHECK_SUCCESS(run, UIWidgetFactory_Construct(&self->Widgets))
        && TEST_CHECK_SUCCESS(run, LabelWidget_RegisterType(&self->Widgets, &self->LabelType));
    self->IsScreenConstructed = IsBuilt && TEST_CHECK_SUCCESS(run, UIScreen_Construct(&self->Screen, &self->Widgets));
    self->IsReady = self->IsScreenConstructed;
    return self->IsReady;
}

static void LabelFixture_Deconstruct(TestRun* run, LabelFixture* self)
{
    if (self->IsScreenConstructed)
    {
        TEST_CHECK_SUCCESS(run, UIScreen_Deconstruct(&self->Screen));
    }
    TEST_CHECK_SUCCESS(run, UIWidgetFactory_Deconstruct(&self->Widgets));
    TEST_CHECK(run, TextComponentFactory_GetLiveComponentCount(&self->Components) == 0U);
    TEST_CHECK_SUCCESS(run, TextComponentFactory_Deconstruct(&self->Components));
    TestAssetFixture_Deconstruct(run, &self->Assets);
}

static TextComponent* CreateText(TestRun* run, LabelFixture* fixture, const char* text)
{
    StringComponent* String = NULL;
    TEST_CHECK_SUCCESS(run, TextComponentFactory_CreateString(&fixture->Components, (const unsigned char*)text, &String));
    return (String != NULL) ? StringComponent_AsComponent(String) : NULL;
}

static LabelWidget* CreateLabel(TestRun* run, LabelFixture* fixture, TextComponent* text)
{
    LabelWidget* Label = NULL;
    if (!TEST_CHECK_SUCCESS(run, LabelWidget_Create(&fixture->Screen, fixture->LabelType, &fixture->Components,
        fixture->Assets.Manager, NULL, text, &Label)))
    {
        return NULL;
    }
    if (!TEST_CHECK_SUCCESS(run, TextDocument_SetDefaultFontName(LabelWidget_GetDocument(Label), (const unsigned char*)u8"mono")))
    {
        TEST_CHECK_SUCCESS(run, Widget_Deconstruct(LabelWidget_AsWidget(Label)));
        return NULL;
    }
    return Label;
}


// Tests.
static void LabelLaysOutItsTextWithItsProperties(TestRun* run)
{
    LabelFixture Fixture;
    if (LabelFixture_Construct(run, &Fixture))
    {
        TextComponent* Text = CreateText(run, &Fixture, "aaa bbb");
        LabelWidget* Label = (Text != NULL) ? CreateLabel(run, &Fixture, Text) : NULL;
        if (Label != NULL)
        {
            Vector2 Size = { 0 };
            TEST_CHECK_SUCCESS(run, LabelWidget_GetDrawSize(Label, &Size));
            TEST_CHECK(run, IsNear(Size.x, 0.35f) && IsNear(Size.y, 0.1f));

            TEST_CHECK_SUCCESS(run, LabelWidget_SetSize(Label, 2.0f));
            TEST_CHECK_SUCCESS(run, LabelWidget_GetDrawSize(Label, &Size));
            TEST_CHECK(run, IsNear(Size.x, 0.7f) && IsNear(Size.y, 0.2f));
            TEST_CHECK_ERROR(run, LabelWidget_SetSize(Label, -1.0f), ErrorCode_ArgumentOutOfRange);

            // Bounds narrower than the text wrap it in the wrap modes and only clip it in the cut mode.
            TEST_CHECK_SUCCESS(run, LabelWidget_SetBounds(Label, (Vector2){ 0.5f, 1.0f }));
            TEST_CHECK_SUCCESS(run, LabelWidget_GetDrawSize(Label, &Size));
            TEST_CHECK(run, IsNear(Size.x, 0.7f));
            TEST_CHECK_SUCCESS(run, LabelWidget_SetBoundHandling(Label, LabelBoundHandling_Wrap));
            TEST_CHECK_SUCCESS(run, LabelWidget_GetDrawSize(Label, &Size));
            TEST_CHECK(run, IsNear(Size.x, 0.3f) && IsNear(Size.y, 0.4f));
            TEST_CHECK_SUCCESS(run, LabelWidget_SetBoundHandling(Label, LabelBoundHandling_Resize));
            TEST_CHECK_SUCCESS(run, LabelWidget_GetDrawSize(Label, &Size));
            TEST_CHECK(run, IsNear(Size.x, 0.5f));

            // An edit inside the tree shows up without telling the label.
            TEST_CHECK_SUCCESS(run, LabelWidget_ClearBounds(Label));
            TEST_CHECK_SUCCESS(run, StringComponent_SetText(TextComponent_AsStringComponent(LabelWidget_GetText(Label)),
                (const unsigned char*)u8"ab"));
            TEST_CHECK_SUCCESS(run, Widget_Update(LabelWidget_AsWidget(Label), ProgramTime_Create(0.1, 0.1)));
            TEST_CHECK_SUCCESS(run, LabelWidget_GetDrawSize(Label, &Size));
            TEST_CHECK(run, IsNear(Size.x, 0.2f));

            TEST_CHECK_SUCCESS(run, LabelWidget_SetText(Label, CreateText(run, &Fixture, "abcd")));
            TEST_CHECK(run, TextComponentFactory_GetLiveComponentCount(&Fixture.Components) == 1U);
            TEST_CHECK_SUCCESS(run, LabelWidget_GetDrawSize(Label, &Size));
            TEST_CHECK(run, IsNear(Size.x, 0.4f));

            TEST_CHECK_SUCCESS(run, Widget_Deconstruct(LabelWidget_AsWidget(Label)));
            TEST_CHECK(run, TextComponentFactory_GetLiveComponentCount(&Fixture.Components) == 0U);
        }
        else if (Text != NULL)
        {
            TEST_CHECK_SUCCESS(run, TextComponentFactory_ReturnTree(&Fixture.Components, Text));
        }
    }
    LabelFixture_Deconstruct(run, &Fixture);
}

static void FailedConstructionLeavesTheTextWithTheCaller(TestRun* run)
{
    LabelFixture Fixture;
    if (LabelFixture_Construct(run, &Fixture))
    {
        TextComponent* Parent = CreateText(run, &Fixture, "parent");
        StringComponent* Child = (Parent != NULL) ? TestText_AddString(run, &Fixture.Components, Parent, "child") : NULL;
        if (Child != NULL)
        {
            LabelWidget* Label = NULL;
            TEST_CHECK_ERROR(run, LabelWidget_Create(&Fixture.Screen, Fixture.LabelType, &Fixture.Components, Fixture.Assets.Manager,
                NULL, StringComponent_AsComponent(Child), &Label), ErrorCode_InvalidOperation);
            TEST_CHECK(run, Label == NULL);
            TEST_CHECK(run, TextComponent_GetParent(StringComponent_AsComponent(Child)) == Parent);
            TEST_CHECK(run, TextComponentFactory_GetLiveComponentCount(&Fixture.Components) == 2U);
        }
        if (Parent != NULL)
        {
            TEST_CHECK_SUCCESS(run, TextComponentFactory_ReturnTree(&Fixture.Components, Parent));
        }
    }
    LabelFixture_Deconstruct(run, &Fixture);
}


// A label's text is held by its document: another label cannot take it, at creation or with SetText.
static void AnotherLabelsTextIsRefused(TestRun* run)
{
    LabelFixture Fixture;
    if (LabelFixture_Construct(run, &Fixture))
    {
        TextComponent* Text = CreateText(run, &Fixture, "mine");
        LabelWidget* Owner = (Text != NULL) ? CreateLabel(run, &Fixture, Text) : NULL;
        LabelWidget* Other = (Owner != NULL) ? CreateLabel(run, &Fixture, NULL) : NULL;
        if (Other != NULL)
        {
            TEST_CHECK_ERROR(run, LabelWidget_SetText(Other, Text), ErrorCode_InvalidOperation);
            LabelWidget* Thief = NULL;
            TEST_CHECK_ERROR(run, LabelWidget_Create(&Fixture.Screen, Fixture.LabelType, &Fixture.Components, Fixture.Assets.Manager,
                NULL, Text, &Thief), ErrorCode_InvalidOperation);
            TEST_CHECK(run, Thief == NULL);
            TEST_CHECK(run, (LabelWidget_GetText(Owner) == Text) && (LabelWidget_GetText(Other) == NULL));
            TEST_CHECK_SUCCESS(run, Widget_Deconstruct(LabelWidget_AsWidget(Other)));
        }
        if (Owner != NULL)
        {
            TEST_CHECK_SUCCESS(run, Widget_Deconstruct(LabelWidget_AsWidget(Owner)));
        }
        else if (Text != NULL)
        {
            TEST_CHECK_SUCCESS(run, TextComponentFactory_ReturnTree(&Fixture.Components, Text));
        }
        TEST_CHECK(run, TextComponentFactory_GetLiveComponentCount(&Fixture.Components) == 0U);
    }
    LabelFixture_Deconstruct(run, &Fixture);
}


// Public functions.
void LabelWidgetTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "label_lays_out_its_text_with_its_properties", LabelLaysOutItsTextWithItsProperties },
        { "failed_construction_leaves_the_text_with_the_caller", FailedConstructionLeavesTheTextWithTheCaller },
        { "another_labels_text_is_refused", AnotherLabelsTextIsRefused },
    };
    TestRun_RunSuite(run, "LabelWidget", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
