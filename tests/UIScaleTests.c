#include "TestSuites.h"
#include "TestUI.h"
#include "LabelWidget.h"
#include "UIKit.h"
#include <math.h>


// Macros.
/** Text 14 UI pixels tall, whose glyphs advance 7 at that size; a button's padding 8 by 4 and its minimum 24 by 24. */
#define SCALE_THEME \
    "{\"name\": \"scaled\", \"font\": \"mono\", \"text_size\": 14, \"elements\": {" \
    "\"button\": {\"padding\": [8, 4], \"min_size\": [24, 24], \"background\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#ffffff\"}}," \
    "\"panel\": {\"padding\": [0, 0], \"background\": {\"kind\": \"none\"}}}}"


// Types.
/* A button look that keeps its text laid out as the standard one does, so the text's size on the surface can be read. */
typedef struct TextProbeLookStruct
{
    UIButtonLookBase Base;
    UIElementText Text;
} TextProbeLook;

/* A container that arranges nothing: its children keep their own placements, in its scale. */
typedef struct HolderWidgetStruct
{
    Widget Base;
} HolderWidget;

/* A column that counts how often it is asked for its preferred size. */
typedef struct CountingColumnStruct
{
    Widget Base;
    int MeasureCount;
    int LayoutCount;
} CountingColumn;

/* A widget that records what its Measure hears, and answers as text does: a width rounded up to whole surface pixels at its
 * effective scale, so its answer in its own UI pixels depends on that scale. */
typedef struct RecordingWidgetStruct
{
    Widget Base;
    int MeasureCount;
    float LastAvailableWidth;
} RecordingWidget;


// Static functions: the probes.
static Error TextProbe_Construct(void* self, void* args)
{
    const UIButtonArgs* Arguments = args;
    return UIElementText_Construct(&((TextProbeLook*)self)->Text, UIKit_GetTextFactory(Arguments->Kit), UIKit_GetAssets(Arguments->Kit),
        NULL);
}

static Error TextProbe_Deconstruct(void* self)
{
    return UIElementText_Deconstruct(&((TextProbeLook*)self)->Text);
}

static Error TextProbe_Update(void* self, ProgramTime time)
{
    (void)time;
    TextProbeLook* Probe = self;
    return UIElementText_Refresh(&Probe->Text, &Probe->Base.Base, &Probe->Base.Button._text);
}

static Error TextProbe_Render(void* self, UIRenderContext* context)
{
    (void)self;
    (void)context;
    return Error_CreateSuccess();
}

/* The standard button's measure: the text, padding 8 by 4, at least 24 by 24, in its own UI pixels. */
static Error TextProbe_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)context;
    (void)availableWidth;
    TextProbeLook* Probe = self;
    Error Result = UIElementText_Refresh(&Probe->Text, &Probe->Base.Base, &Probe->Base.Button._text);
    Vector2 Pixels = UIElementText_GetPixelSize(&Probe->Text, Widget_GetScreen(&Probe->Base.Base));
    float Scale = Widget_GetEffectiveScale(&Probe->Base.Base);
    *outSize = (Vector2){ .x = fmaxf((Pixels.x / Scale) + 16.0f, 24.0f), .y = fmaxf((Pixels.y / Scale) + 8.0f, 24.0f) };
    return Result;
}

static const UILookVTable TextProbeVTable =
{
    .Construct = TextProbe_Construct,
    .Deconstruct = TextProbe_Deconstruct,
    .Update = TextProbe_Update,
    .Render = TextProbe_Render,
    .Measure = TextProbe_Measure
};

static Error CountingColumn_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    CountingColumn* Column = self;
    Column->MeasureCount++;
    UIStackLayout Layout = UIStackOptions_Create(UIStackDirection_Column).Layout;
    return UIStackLayout_Measure(&Layout, &Column->Base, context, availableWidth, outSize);
}

static Error CountingColumn_Layout(void* self, const UILayoutContext* context)
{
    CountingColumn* Column = self;
    Column->LayoutCount++;
    UIStackLayout Layout = UIStackOptions_Create(UIStackDirection_Column).Layout;
    return UIStackLayout_Arrange(&Layout, &Column->Base, context);
}

/* 7.3 UI pixels of its own, rounded up to whole surface pixels, by 10. */
static Error Recording_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)context;
    RecordingWidget* Recording = self;
    Recording->MeasureCount++;
    Recording->LastAvailableWidth = availableWidth;
    float Scale = Widget_GetEffectiveScale(&Recording->Base);
    *outSize = (Vector2){ .x = ceilf(7.3f * Scale) / Scale, .y = 10.0f };
    return Error_CreateSuccess();
}

static const WidgetVTable HolderVTable = { .Render = NULL };
static const WidgetVTable CountingColumnVTable = { .Measure = CountingColumn_Measure, .Layout = CountingColumn_Layout };
static const WidgetVTable RecordingVTable = { .Measure = Recording_Measure };


// Static functions: building.
static bool Construct(TestRun* run, TestUIFixture* fixture)
{
    return TestUI_Construct(run, fixture) && TestUI_SetTheme(run, fixture, SCALE_THEME)
        && TEST_CHECK_SUCCESS(run, UIKit_RegisterLook(&fixture->Kit, UIElementKind_Button, u8"tbp:text_probe", sizeof(TextProbeLook),
            &TextProbeVTable, NULL));
}

static Widget* Get(TestUIFixture* fixture, uint64_t id)
{
    Widget* Target = NULL;
    UIScreen_TryGetWidgetById(&fixture->Screen, id, &Target);
    return Target;
}

/* A text probe button "ab" with a scale, placed (or sized by its content in a stack when @p isPlaced is false). */
static uint64_t AddProbe(TestRun* run, TestUIFixture* fixture, uint64_t parent, bool isPlaced, float width, float height, float scale)
{
    UIButtonOptions Options = UIButtonOptions_Create(u8"ab");
    Options.Element.Look = u8"tbp:text_probe";
    Options.Element.Scale = scale;
    Options.Element.HasPlacement = isPlaced;
    Options.Element.Placement = UIPlacement_FromTopLeft(10.0f, 10.0f, width, height);
    Options.Element.Hint.Align = UIStackAlign_Start;
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&fixture->Kit, &fixture->Screen, parent, &Options, &Id));
    return Id;
}

/* The text's height on the surface: its line, so the text size in surface pixels. */
static float GetTextPixels(TestUIFixture* fixture, uint64_t id)
{
    Widget* Target = Get(fixture, id);
    return (Target != NULL) ? UIElementText_GetPixelSize(&((TextProbeLook*)Target)->Text, &fixture->Screen).y : -1.0f;
}

/* A holder filling the surface (or a column when it counts), with a scale. */
static uint64_t AddHolder(TestRun* run, TestUIFixture* fixture, uint64_t type, uint64_t parent, float scale)
{
    Widget* Created = NULL;
    if (!TEST_CHECK_SUCCESS(run, UIWidgetFactory_CreateWidget(&fixture->Widgets, &fixture->Screen, type, NULL, &Created)))
    {
        return WIDGET_ID_INVALID;
    }
    uint64_t Id = Widget_GetId(Created);
    UIPlacement Whole = UIPlacement_Stretch(UIInsets_All(0.0f));
    TEST_CHECK_SUCCESS(run, Widget_SetPlacement(Created, &Whole));
    TEST_CHECK_SUCCESS(run, Widget_SetScale(Created, scale));
    TEST_CHECK_SUCCESS(run, Widget_InitializeWidget(Created));
    Widget* Parent = NULL;
    if (parent == WIDGET_ID_INVALID)
    {
        TEST_CHECK_SUCCESS(run, UIScreen_AddWidget(&fixture->Screen, Created));
    }
    else if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&fixture->Screen, parent, &Parent)))
    {
        TEST_CHECK_SUCCESS(run, Widget_AddSubWidget(Parent, Created));
    }
    return Id;
}

static uint64_t RegisterType(TestRun* run, TestUIFixture* fixture, const WidgetVTable* vtable, size_t size, const unsigned char* name)
{
    WidgetTypeDescriptor Descriptor = { .Name = name, .StructSize = size, .VTable = vtable };
    uint64_t Type = WIDGET_TYPE_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIWidgetFactory_RegisterType(&fixture->Widgets, &Descriptor, &Type));
    return Type;
}

static bool IsWhole(float value)
{
    return fabsf(value - roundf(value)) < 0.001f;
}


// Tests.
/* The table of the element's design: size from where it sits, scale for what is drawn in it. */
static void SizeAndScaleFollowTheTable(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Button = AddProbe(run, &Fixture, WIDGET_ID_INVALID, true, 100.0f, 24.0f, 1.0f);
        uint64_t Long = AddProbe(run, &Fixture, WIDGET_ID_INVALID, true, 400.0f, 24.0f, 1.0f);
        uint64_t BigFixed = AddProbe(run, &Fixture, WIDGET_ID_INVALID, true, 100.0f, 24.0f, 2.0f);
        uint64_t Column = WIDGET_ID_INVALID;
        UIStackOptions ColumnOptions = UIStackOptions_Create(UIStackDirection_Column);
        ColumnOptions.Element.HasPlacement = true;
        ColumnOptions.Element.Placement = UIPlacement_Stretch(UIInsets_All(0.0f));
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &ColumnOptions, &Column));
        uint64_t Content = AddProbe(run, &Fixture, Column, false, 0.0f, 0.0f, 1.0f);
        uint64_t BigContent = AddProbe(run, &Fixture, Column, false, 0.0f, 0.0f, 2.0f);
        uint64_t Window = AddHolder(run, &Fixture, RegisterType(run, &Fixture, &HolderVTable, sizeof(HolderWidget), u8"Holder"),
            WIDGET_ID_INVALID, 2.0f);
        uint64_t InWindow = AddProbe(run, &Fixture, Window, true, 100.0f, 24.0f, 1.0f);
        TestUI_Ticks(run, &Fixture, 3U);

        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Button), 10.0f, 10.0f, 100.0f, 24.0f) && (GetTextPixels(&Fixture, Button) == 14.0f));
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Long), 10.0f, 10.0f, 400.0f, 24.0f) && (GetTextPixels(&Fixture, Long) == 14.0f));
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, BigFixed), 10.0f, 10.0f, 100.0f, 24.0f));
        TEST_CHECK(run, GetTextPixels(&Fixture, BigFixed) == 28.0f);
        Rectangle Small = TestUI_GetPixelBox(run, &Fixture, Content);
        Rectangle Big = TestUI_GetPixelBox(run, &Fixture, BigContent);
        TEST_CHECK(run, TestUI_IsBox(Small, 0.0f, 0.0f, 30.0f, 24.0f) && (GetTextPixels(&Fixture, Content) == 14.0f));
        TEST_CHECK(run, TestUI_IsBox(Big, 0.0f, 24.0f, 60.0f, 48.0f) && (GetTextPixels(&Fixture, BigContent) == 28.0f));
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, InWindow), 20.0f, 20.0f, 200.0f, 48.0f));
        TEST_CHECK(run, GetTextPixels(&Fixture, InWindow) == 28.0f);
        TEST_CHECK(run, Widget_GetEffectiveScale(Get(&Fixture, InWindow)) == 2.0f);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Scale multiplies down the tree, a child's fixed size counts in its parent's scale, and the screen's scale is in it too. */
static void ScaleMultipliesDownTheTree(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Holder = RegisterType(run, &Fixture, &HolderVTable, sizeof(HolderWidget), u8"Holder");
        uint64_t Outer = AddHolder(run, &Fixture, Holder, WIDGET_ID_INVALID, 2.0f);
        uint64_t Inner = AddHolder(run, &Fixture, Holder, Outer, 1.5f);
        UIStackOptions RowOptions = UIStackOptions_Create(UIStackDirection_Row);
        RowOptions.Layout.Padding = UIInsets_All(2.0f);
        RowOptions.Layout.Spacing = 4.0f;
        RowOptions.Element.HasPlacement = true;
        RowOptions.Element.Placement = UIPlacement_Stretch(UIInsets_All(0.0f));
        uint64_t Row = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, Inner, &RowOptions, &Row));
        uint64_t First = AddProbe(run, &Fixture, Row, false, 0.0f, 0.0f, 1.0f);
        UIButtonOptions FixedOptions = UIButtonOptions_Create(u8"x");
        FixedOptions.Element.Look = u8"tbp:text_probe";
        FixedOptions.Element.Hint = UIStackHint_Fixed(10.0f);
        FixedOptions.Element.Hint.Align = UIStackAlign_Start;
        uint64_t Fixed = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, Row, &FixedOptions, &Fixed));
        TestUI_Ticks(run, &Fixture, 3U);
        TEST_CHECK(run, Widget_GetEffectiveScale(Get(&Fixture, First)) == 3.0f);
        // The row's padding and spacing count in its scale (3): its first child at 6, 6, 30 x 24 UI pixels of its own (3) = 90 x 72.
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, First), 6.0f, 6.0f, 90.0f, 72.0f));
        TEST_CHECK(run, TestUI_IsSamePixel(GetTextPixels(&Fixture, First), 42.0f));
        // A fixed 10 in the row is 30 surface pixels, 4 of spacing after the first: at 6 + 90 + 12.
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Fixed), 108.0f, 6.0f, 30.0f, 72.0f));

        // The screen's scale is in it, at once and for what is under it.
        TEST_CHECK_SUCCESS(run, UIScreen_SetScale(&Fixture.Screen, 0.5f));
        TEST_CHECK(run, Widget_GetEffectiveScale(Get(&Fixture, First)) == 1.5f);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, TestUI_IsSamePixel(GetTextPixels(&Fixture, First), 21.0f));
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, First), 3.0f, 3.0f, 45.0f, 36.0f));

        // A widget's own scale changed: its subtree follows, a widget moved under another takes that one's scale.
        TEST_CHECK_SUCCESS(run, UIElement_SetScale(&Fixture.Screen, Inner, 3.0f));
        TEST_CHECK(run, Widget_GetEffectiveScale(Get(&Fixture, First)) == 3.0f);
        TEST_CHECK_SUCCESS(run, Widget_MoveTo(Get(&Fixture, Row), Get(&Fixture, Outer)));
        TEST_CHECK(run, Widget_GetEffectiveScale(Get(&Fixture, First)) == 1.0f);
        TEST_CHECK_SUCCESS(run, Widget_MoveTo(Get(&Fixture, Row), NULL));
        TEST_CHECK(run, Widget_GetEffectiveScale(Get(&Fixture, First)) == 0.5f);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, TestUI_IsSamePixel(GetTextPixels(&Fixture, First), 7.0f));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* The standard looks scale whole: a panel's padding, a button's box and its nine-slice corners come from the effective scale. */
static void TheStandardLooksScaleWhole(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIPanelOptions PanelOptions = UIPanelOptions_Create();
        PanelOptions.HasPadding = true;
        PanelOptions.Padding = UIInsets_All(5.0f);
        PanelOptions.Element.HasPlacement = true;
        PanelOptions.Element.Placement = UIPlacement_FromTopLeft(0.0f, 0.0f, 200.0f, 200.0f);
        PanelOptions.Element.Scale = 2.0f;
        uint64_t Panel = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreatePanel(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &PanelOptions, &Panel));
        UIButtonOptions Options = UIButtonOptions_Create(u8"ab");
        Options.Element.Hint.Align = UIStackAlign_Start;
        uint64_t Button = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, Panel, &Options, &Button));
        TestUI_Ticks(run, &Fixture, 3U);
        // The panel keeps its placed box (in the screen's scale); inside it everything is doubled: padding 10, the button 60 x 48.
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Panel), 0.0f, 0.0f, 200.0f, 200.0f));
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Button), 10.0f, 10.0f, 60.0f, 48.0f));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Wrapped labels under a scale settle: once laid out, 100 idle ticks ask no Measure and lay out nothing anew. */
static void WrappedLabelsUnderAScaleSettle(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Type = RegisterType(run, &Fixture, &CountingColumnVTable, sizeof(CountingColumn), u8"CountingColumn");
        UIStackOptions RowOptions = UIStackOptions_Create(UIStackDirection_Row);
        RowOptions.Element.HasPlacement = true;
        RowOptions.Element.Placement = UIPlacement_Stretch(UIInsets_All(0.0f));
        RowOptions.Element.Scale = 1.25f;
        uint64_t Row = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &RowOptions, &Row));
        uint64_t Columns[2] = { WIDGET_ID_INVALID, WIDGET_ID_INVALID };
        for (size_t Index = 0U; Index < 2U; Index++)
        {
            Widget* Created = NULL;
            if (TEST_CHECK_SUCCESS(run, UIWidgetFactory_CreateWidget(&Fixture.Widgets, &Fixture.Screen, Type, NULL, &Created)))
            {
                Columns[Index] = Widget_GetId(Created);
                UIStackHint Fill = UIStackHint_Fill((float)(Index + 1U));
                TEST_CHECK_SUCCESS(run, Widget_SetStackHint(Created, &Fill));
                TEST_CHECK_SUCCESS(run, Widget_SetScale(Created, (Index == 0U) ? 1.5f : 0.75f));
                TEST_CHECK_SUCCESS(run, Widget_InitializeWidget(Created));
                TEST_CHECK_SUCCESS(run, Widget_AddSubWidget(Get(&Fixture, Row), Created));
            }
            UILabelOptions Label = UILabelOptions_Create(u8"some words that wrap in a narrow column of a row that is scaled");
            Label.IsWrapped = true;
            Label.Element.Hint.Align = UIStackAlign_Stretch;
            uint64_t LabelId = WIDGET_ID_INVALID;
            TEST_CHECK_SUCCESS(run, UIKit_CreateLabel(&Fixture.Kit, &Fixture.Screen, Columns[Index], &Label, &LabelId));
        }
        TestUI_Ticks(run, &Fixture, 5U);
        CountingColumn* First = (CountingColumn*)Get(&Fixture, Columns[0]);
        CountingColumn* Second = (CountingColumn*)Get(&Fixture, Columns[1]);
        if (TEST_CHECK(run, (First != NULL) && (Second != NULL)))
        {
            int Measured = First->MeasureCount + Second->MeasureCount;
            int LaidOut = First->LayoutCount + Second->LayoutCount;
            TestUI_Ticks(run, &Fixture, 100U);
            TEST_CHECK(run, (First->MeasureCount + Second->MeasureCount) == Measured);
            // Layout hooks run every tick by design; what must not come back is the measuring a layout asks for.
            TEST_CHECK(run, (First->LayoutCount + Second->LayoutCount) == (LaidOut + 200));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Scales that are not numbers or out of bounds are refused; nesting the largest keeps the effective scale to its bounds, and every
 * box stays a number. */
static void HostileScalesAreRefusedOrKept(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Holder = RegisterType(run, &Fixture, &HolderVTable, sizeof(HolderWidget), u8"Holder");
        uint64_t Parent = AddHolder(run, &Fixture, Holder, WIDGET_ID_INVALID, 1.0f);
        Widget* Target = Get(&Fixture, Parent);
        const float Bad[] = { NAN, INFINITY, -1.0f, 0.0f, WIDGET_MIN_SCALE * 0.5f, WIDGET_MAX_SCALE * 1.01f, 1e30f };
        for (size_t Index = 0U; Index < (sizeof(Bad) / sizeof(Bad[0])); Index++)
        {
            TEST_CHECK_ERROR(run, Widget_SetScale(Target, Bad[Index]), ErrorCode_ArgumentOutOfRange);
        }
        TEST_CHECK(run, Widget_GetScale(Target) == 1.0f);
        TEST_CHECK_ERROR(run, Widget_SetScale(NULL, 2.0f), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, UIElement_SetScale(&Fixture.Screen, 12345U, 2.0f), ErrorCode_InvalidOperation);
        UIButtonOptions BadOptions = UIButtonOptions_Create(u8"x");
        BadOptions.Element.Scale = NAN;
        uint64_t Refused = WIDGET_ID_INVALID;
        TEST_CHECK_ERROR(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &BadOptions, &Refused), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, Refused == WIDGET_ID_INVALID);

        // The largest nested: kept to the effective scale's bounds, the smallest too.
        TEST_CHECK_SUCCESS(run, UIScreen_SetScale(&Fixture.Screen, UI_SCREEN_MAX_SCALE));
        uint64_t Chain[7] = { Parent, 0U, 0U, 0U, 0U, 0U, 0U };
        TEST_CHECK_SUCCESS(run, Widget_SetScale(Target, WIDGET_MAX_SCALE));
        for (size_t Level = 1U; Level < 7U; Level++)
        {
            Chain[Level] = AddHolder(run, &Fixture, Holder, Chain[Level - 1U], WIDGET_MAX_SCALE);
        }
        uint64_t Deep = AddProbe(run, &Fixture, Chain[6], true, 50.0f, 20.0f, WIDGET_MAX_SCALE);
        TEST_CHECK(run, Widget_GetEffectiveScale(Get(&Fixture, Deep)) == WIDGET_MAX_EFFECTIVE_SCALE);
        TestUI_Ticks(run, &Fixture, 2U);
        Rectangle Box = TestUI_GetPixelBox(run, &Fixture, Deep);
        TEST_CHECK(run, isfinite(Box.x) && isfinite(Box.y) && isfinite(Box.width) && isfinite(Box.height));
        TEST_CHECK(run, isfinite(GetTextPixels(&Fixture, Deep)));
        TEST_CHECK_SUCCESS(run, UIScreen_SetScale(&Fixture.Screen, UI_SCREEN_MIN_SCALE));
        for (size_t Level = 0U; Level < 7U; Level++)
        {
            TEST_CHECK_SUCCESS(run, Widget_SetScale(Get(&Fixture, Chain[Level]), WIDGET_MIN_SCALE));
        }
        TEST_CHECK_SUCCESS(run, Widget_SetScale(Get(&Fixture, Deep), WIDGET_MIN_SCALE));
        TEST_CHECK(run, Widget_GetEffectiveScale(Get(&Fixture, Deep)) == WIDGET_MIN_EFFECTIVE_SCALE);
        TEST_CHECK(run, Widget_GetEffectiveScale(Get(&Fixture, Deep)) >= WIDGET_MIN_EFFECTIVE_SCALE);
        TestUI_Ticks(run, &Fixture, 2U);
        Box = TestUI_GetPixelBox(run, &Fixture, Deep);
        TEST_CHECK(run, isfinite(Box.width) && isfinite(Box.height));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Where the effective scale is kept to its bounds, the box still fits what is drawn: two labels whose own scales differ but whose
 * effective scales are both the largest get the same box, as they draw the same text. */
static void AKeptScaleKeepsTheBoxToWhatIsDrawn(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        TEST_CHECK_SUCCESS(run, UIScreen_SetScale(&Fixture.Screen, UI_SCREEN_MAX_SCALE));
        UIStackOptions RowOptions = UIStackOptions_Create(UIStackDirection_Row);
        RowOptions.Element.HasPlacement = true;
        RowOptions.Element.Placement = UIPlacement_Stretch(UIInsets_All(0.0f));
        RowOptions.Element.Scale = WIDGET_MAX_SCALE;
        uint64_t Row = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &RowOptions, &Row));
        uint64_t Labels[2] = { WIDGET_ID_INVALID, WIDGET_ID_INVALID };
        for (size_t Index = 0U; Index < 2U; Index++)
        {
            UILabelOptions Options = UILabelOptions_Create(u8"ab");
            Options.Element.Scale = (float)(Index + 1U);
            Options.Element.Hint.Align = UIStackAlign_Start;
            TEST_CHECK_SUCCESS(run, UIKit_CreateLabel(&Fixture.Kit, &Fixture.Screen, Row, &Options, &Labels[Index]));
        }
        TestUI_Ticks(run, &Fixture, 3U);
        TEST_CHECK(run, (Widget_GetEffectiveScale(Get(&Fixture, Labels[0])) == WIDGET_MAX_EFFECTIVE_SCALE)
            && (Widget_GetEffectiveScale(Get(&Fixture, Labels[1])) == WIDGET_MAX_EFFECTIVE_SCALE));
        Rectangle First = TestUI_GetPixelBox(run, &Fixture, Labels[0]);
        Rectangle Second = TestUI_GetPixelBox(run, &Fixture, Labels[1]);
        TEST_CHECK(run, TestUI_IsSamePixel(First.width, Second.width) && TestUI_IsSamePixel(First.height, Second.height));
        TEST_CHECK(run, TestUI_IsSamePixel(First.height, 14.0f * WIDGET_MAX_EFFECTIVE_SCALE)); // 14 UI pixels of text, at 64.
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* The scale is a typed property: animated, a content-sized box grows with it. */
static void AnimatingTheScaleGrowsAContentBox(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIStackOptions ColumnOptions = UIStackOptions_Create(UIStackDirection_Column);
        ColumnOptions.Element.HasPlacement = true;
        ColumnOptions.Element.Placement = UIPlacement_Stretch(UIInsets_All(0.0f));
        uint64_t Column = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &ColumnOptions, &Column));
        uint64_t Button = AddProbe(run, &Fixture, Column, false, 0.0f, 0.0f, 1.0f);
        TestUI_Ticks(run, &Fixture, 2U);
        UIKeyframe Frames[2] =
        {
            { .Time = 0.0, .Interpolation = UIInterpolation_Linear, .CustomEasing = NULL, .Value = { .FloatValue = 1.0f } },
            { .Time = 0.1, .Interpolation = UIInterpolation_Linear, .CustomEasing = NULL, .Value = { .FloatValue = 2.0f } }
        };
        uint64_t Animation = 0U;
        TEST_CHECK_SUCCESS(run, UIScreen_StartAnimation(&Fixture.Screen, Button, WidgetBaseProperty_Scale, UIPropertyType_Float, Frames, 2U,
            UIAnimationOptions_CreateDefault(), &Animation));
        TestUI_Ticks(run, &Fixture, 24U); // Half way.
        float Half = Widget_GetScale(Get(&Fixture, Button));
        TEST_CHECK(run, (Half > 1.3f) && (Half < 1.7f));
        TestUI_Ticks(run, &Fixture, 40U);
        TEST_CHECK(run, Widget_GetScale(Get(&Fixture, Button)) == 2.0f);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Button), 0.0f, 0.0f, 60.0f, 48.0f));
        UIPropertyValue Value;
        TEST_CHECK_SUCCESS(run, Widget_GetProperty(Get(&Fixture, Button), WidgetBaseProperty_Scale, &Value));
        TEST_CHECK(run, (Value.Type == UIPropertyType_Float) && (Value.Data.FloatValue == 2.0f));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* At scales that are no whole number every edge still lands on a whole surface pixel, and neighbours share theirs. */
static void OddScalesKeepWholePixels(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        TEST_CHECK_SUCCESS(run, UIScreen_SetScale(&Fixture.Screen, 1.25f));
        UIStackOptions ColumnOptions = UIStackOptions_Create(UIStackDirection_Column);
        ColumnOptions.Layout.Spacing = 3.0f;
        ColumnOptions.Layout.Padding = UIInsets_All(7.0f);
        ColumnOptions.Element.HasPlacement = true;
        ColumnOptions.Element.Placement = UIPlacement_Stretch(UIInsets_All(0.0f));
        ColumnOptions.Element.Scale = 1.3f;
        uint64_t Column = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &ColumnOptions, &Column));
        uint64_t Ids[4];
        const float Scales[4] = { 1.0f, 1.7f, 0.9f, 2.3f };
        for (size_t Index = 0U; Index < 4U; Index++)
        {
            Ids[Index] = AddProbe(run, &Fixture, Column, false, 0.0f, 0.0f, Scales[Index]);
        }
        TestUI_Ticks(run, &Fixture, 3U);
        for (size_t Index = 0U; Index < 4U; Index++)
        {
            Rectangle Box = TestUI_GetPixelBox(run, &Fixture, Ids[Index]);
            TEST_CHECK(run, IsWhole(Box.x) && IsWhole(Box.y) && IsWhole(Box.width) && IsWhole(Box.height));
            TEST_CHECK(run, (Box.width > 0.0f) && (Box.height > 0.0f));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}


/* Measuring converts at once, asked anew or answered from the cache: the hook hears and answers in the widget's own UI pixels, the
 * caller in its parent's. A changed scale asks the widget again (its parent's scale too), and the parent with it. */
static void MeasuringConvertsCachedOrNot(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t ColumnType = RegisterType(run, &Fixture, &CountingColumnVTable, sizeof(CountingColumn), u8"CountingColumn");
        uint64_t RecordingType = RegisterType(run, &Fixture, &RecordingVTable, sizeof(RecordingWidget), u8"Recording");
        uint64_t ColumnId = AddHolder(run, &Fixture, ColumnType, WIDGET_ID_INVALID, 1.0f);
        uint64_t RecordingId = AddHolder(run, &Fixture, RecordingType, ColumnId, 2.0f);
        CountingColumn* Column = (CountingColumn*)Get(&Fixture, ColumnId);
        RecordingWidget* Recording = (RecordingWidget*)Get(&Fixture, RecordingId);
        if (TEST_CHECK(run, (Column != NULL) && (Recording != NULL)))
        {
            RenderContext Measure;
            RenderContext_CreateMeasuring(&Measure, (Vector2){ .x = TEST_UI_SURFACE_WIDTH, .y = TEST_UI_SURFACE_HEIGHT },
                TEST_UI_SURFACE_WIDTH / TEST_UI_SURFACE_HEIGHT, RenderTargetPosition_Centered());
            UIMeasureContext Context = { .Measure = &Measure };
            Vector2 Size = { .x = 0.0f, .y = 0.0f };
            // Asked anew: 200 of the parent's are 100 of its own; 7.5 x 10 of its own (15 surface pixels wide at 2) are 15 x 20.
            TEST_CHECK_SUCCESS(run, Widget_Measure(&Recording->Base, &Context, 200.0f, &Size));
            TEST_CHECK(run, (Recording->MeasureCount == 1) && (Recording->LastAvailableWidth == 100.0f));
            TEST_CHECK(run, TestUI_IsSamePixel(Size.x, 15.0f) && TestUI_IsSamePixel(Size.y, 20.0f));
            // From the cache: the same answer, in the same units.
            TEST_CHECK_SUCCESS(run, Widget_Measure(&Recording->Base, &Context, 200.0f, &Size));
            TEST_CHECK(run, (Recording->MeasureCount == 1) && TestUI_IsSamePixel(Size.x, 15.0f) && TestUI_IsSamePixel(Size.y, 20.0f));
            TEST_CHECK_SUCCESS(run, Widget_Measure(&Column->Base, &Context, 200.0f, &Size));
            TEST_CHECK(run, (Column->MeasureCount == 1) && TestUI_IsSamePixel(Size.x, 15.0f) && TestUI_IsSamePixel(Size.y, 20.0f));

            // Its own scale changed: it is asked again, and so is the column holding it.
            TEST_CHECK_SUCCESS(run, Widget_SetScale(&Recording->Base, 4.0f));
            TEST_CHECK_SUCCESS(run, Widget_Measure(&Column->Base, &Context, 200.0f, &Size));
            TEST_CHECK(run, (Column->MeasureCount == 2) && (Recording->MeasureCount == 2) && (Recording->LastAvailableWidth == 50.0f));
            TEST_CHECK(run, TestUI_IsSamePixel(Size.x, 30.0f) && TestUI_IsSamePixel(Size.y, 40.0f));

            // Its parent's scale changed: its answer in its own UI pixels is another now (44 surface pixels at 6, 30 at 4), so it is
            // asked again too, at the same width of its own.
            TEST_CHECK_SUCCESS(run, Widget_SetScale(&Column->Base, 1.5f));
            TEST_CHECK_SUCCESS(run, Widget_Measure(&Recording->Base, &Context, 200.0f, &Size));
            TEST_CHECK(run, (Recording->MeasureCount == 3) && (Recording->LastAvailableWidth == 50.0f));
            TEST_CHECK(run, TestUI_IsSamePixel(Size.x, 4.0f * (44.0f / 6.0f)) && TestUI_IsSamePixel(Size.y, 40.0f));
            // Its scale as a property is its own, not the effective one.
            UIPropertyValue Value;
            TEST_CHECK_SUCCESS(run, Widget_GetProperty(&Recording->Base, WidgetBaseProperty_Scale, &Value));
            TEST_CHECK(run, (Value.Data.FloatValue == 4.0f) && (Widget_GetEffectiveScale(&Recording->Base) == 6.0f));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A scaled stack shares its own room: what fills it, and what stretches across it, get its box in its own UI pixels. */
static void AScaledStackSharesItsOwnRoom(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIStackOptions RowOptions = UIStackOptions_Create(UIStackDirection_Row);
        RowOptions.Element.HasPlacement = true;
        RowOptions.Element.Placement = UIPlacement_FromTopLeft(0.0f, 0.0f, 400.0f, 100.0f);
        RowOptions.Element.Scale = 2.0f;
        uint64_t Row = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &RowOptions, &Row));
        uint64_t Content = AddProbe(run, &Fixture, Row, false, 0.0f, 0.0f, 1.0f);
        UIButtonOptions FillOptions = UIButtonOptions_Create(u8"x");
        FillOptions.Element.Look = u8"tbp:text_probe";
        FillOptions.Element.Hint = UIStackHint_Fill(1.0f);
        FillOptions.Element.Hint.Align = UIStackAlign_Stretch;
        uint64_t Fill = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, Row, &FillOptions, &Fill));
        TestUI_Ticks(run, &Fixture, 3U);
        // The row is 200 x 50 of its own: the content child 30 x 24 of them (60 x 48), the filling one the 170 x 50 left (340 x 100).
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Content), 0.0f, 0.0f, 60.0f, 48.0f));
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Fill), 60.0f, 0.0f, 340.0f, 100.0f));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A label's own scale sizes its text, and a wrapped one wraps at its box on the surface. */
static void ALabelsOwnScaleSizesAndWrapsItsText(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIStackOptions ColumnOptions = UIStackOptions_Create(UIStackDirection_Column);
        ColumnOptions.Element.HasPlacement = true;
        ColumnOptions.Element.Placement = UIPlacement_FromTopLeft(0.0f, 0.0f, 200.0f, 400.0f);
        uint64_t Column = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &ColumnOptions, &Column));
        uint64_t Ids[3] = { WIDGET_ID_INVALID, WIDGET_ID_INVALID, WIDGET_ID_INVALID };
        for (size_t Index = 0U; Index < 3U; Index++)
        {
            UILabelOptions Options = UILabelOptions_Create((Index < 2U) ? u8"ab" : u8"some words that wrap in the box of a scaled label");
            Options.Element.Scale = (Index == 0U) ? 1.0f : 2.0f;
            Options.Element.Hint.Align = (Index < 2U) ? UIStackAlign_Start : UIStackAlign_Stretch;
            Options.IsWrapped = Index == 2U;
            TEST_CHECK_SUCCESS(run, UIKit_CreateLabel(&Fixture.Kit, &Fixture.Screen, Column, &Options, &Ids[Index]));
        }
        TestUI_Ticks(run, &Fixture, 3U);
        // 14 UI pixels of text: 14 surface pixels tall at 1, 28 at 2.
        Rectangle Plain = TestUI_GetPixelBox(run, &Fixture, Ids[0]);
        Rectangle Big = TestUI_GetPixelBox(run, &Fixture, Ids[1]);
        TEST_CHECK(run, TestUI_IsSamePixel(Plain.height, 14.0f) && TestUI_IsSamePixel(Big.height, 28.0f));
        TEST_CHECK(run, TestUI_IsSamePixel(Big.width, 2.0f * Plain.width));
        // The wrapped one is laid out within its box of 200 surface pixels (the bounds are in fitted units, 500 pixels each).
        LabelWidget* Wrapped = (LabelWidget*)Get(&Fixture, Ids[2]);
        Rectangle WrappedBox = TestUI_GetPixelBox(run, &Fixture, Ids[2]);
        if (TEST_CHECK(run, (Wrapped != NULL) && LabelWidget_HasBounds(Wrapped)))
        {
            TEST_CHECK(run, TestUI_IsSamePixel(WrappedBox.width, 200.0f));
            TEST_CHECK(run, (LabelWidget_GetBounds(Wrapped).x * TEST_UI_SURFACE_HEIGHT) <= (WrappedBox.width + 1.0f));
            TEST_CHECK(run, WrappedBox.height >= 56.0f); // At least two lines of 28.
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* An icon scales with its button, and a slider's thumb with its slider: the drag knows the thumb's length on the surface. */
static void IconsAndThumbsScaleWithTheirElements(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIStackOptions ColumnOptions = UIStackOptions_Create(UIStackDirection_Column);
        ColumnOptions.Element.HasPlacement = true;
        ColumnOptions.Element.Placement = UIPlacement_FromTopLeft(0.0f, 0.0f, 400.0f, 200.0f);
        uint64_t Column = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &ColumnOptions, &Column));
        uint64_t Buttons[2] = { WIDGET_ID_INVALID, WIDGET_ID_INVALID };
        for (size_t Index = 0U; Index < 2U; Index++)
        {
            UIButtonOptions Options = UIButtonOptions_Create(u8"ab");
            Options.Icon = UIElementIcon_FromLetter((uint32_t)'S');
            Options.Element.Scale = (float)(Index + 1U);
            Options.Element.Hint.Align = UIStackAlign_Start;
            TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, Column, &Options, &Buttons[Index]));
        }
        UISliderOptions SliderOptions = UISliderOptions_Create();
        SliderOptions.Maximum = 10.0;
        SliderOptions.Element.HasPlacement = true;
        SliderOptions.Element.Placement = UIPlacement_FromTopLeft(100.0f, 300.0f, 200.0f, 24.0f);
        SliderOptions.Element.Scale = 2.0f;
        uint64_t Slider = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateSlider(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &SliderOptions, &Slider));
        TestUI_Ticks(run, &Fixture, 3U);
        // The icon, the gap and the text all double: the whole button is twice as wide on the surface (30 would be the text alone).
        Rectangle Small = TestUI_GetPixelBox(run, &Fixture, Buttons[0]);
        Rectangle Big = TestUI_GetPixelBox(run, &Fixture, Buttons[1]);
        TEST_CHECK(run, (Small.width >= 40.0f) && (fabsf(Big.width - (2.0f * Small.width)) <= 1.0f));

        // The thumb is 12 of the slider's own UI pixels, 24 on the surface: a press on the track a quarter of the way along what is
        // left for the thumb's centre (100 + 12 + 44) means a quarter of its range.
        TestUI_MovePointer(&Fixture, 156.0f, 312.0f);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        double Value = -1.0;
        TEST_CHECK_SUCCESS(run, UISlider_GetValue(&Fixture.Screen, Slider, &Value));
        TEST_CHECK(run, fabs(Value - 2.5) < 0.001);
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Public functions.
void UIScaleTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "size_and_scale_follow_the_table", SizeAndScaleFollowTheTable },
        { "scale_multiplies_down_the_tree", ScaleMultipliesDownTheTree },
        { "the_standard_looks_scale_whole", TheStandardLooksScaleWhole },
        { "wrapped_labels_under_a_scale_settle", WrappedLabelsUnderAScaleSettle },
        { "hostile_scales_are_refused_or_kept", HostileScalesAreRefusedOrKept },
        { "animating_the_scale_grows_a_content_box", AnimatingTheScaleGrowsAContentBox },
        { "odd_scales_keep_whole_pixels", OddScalesKeepWholePixels },
        { "measuring_converts_cached_or_not", MeasuringConvertsCachedOrNot },
        { "a_scaled_stack_shares_its_own_room", AScaledStackSharesItsOwnRoom },
        { "a_labels_own_scale_sizes_and_wraps_its_text", ALabelsOwnScaleSizesAndWrapsItsText },
        { "icons_and_thumbs_scale_with_their_elements", IconsAndThumbsScaleWithTheirElements },
        { "a_kept_scale_keeps_the_box_to_what_is_drawn", AKeptScaleKeepsTheBoxToWhatIsDrawn },
    };
    TestRun_RunSuite(run, "UIScale", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
