#include "TestSuites.h"
#include "TestUI.h"
#include "LabelWidget.h"
#include "UIInternal.h"
#include "UIPlacement.h"
#include "wr/WRCompile.h"
#include <float.h>
#include <math.h>


// Macros.
/** A theme with only a font and a text size. */
#define PLAIN_THEME "{\"name\": \"plain\", \"font\": \"mono\", \"text_size\": 14}"


// Types.
/* A widget with a preferred size of its own, which counts how often it was asked. */
typedef struct SizedWidgetStruct
{
    Widget Base;
    Vector2 Preferred;
    /* When set, the preferred height follows the width: Area / width, as text that wraps. */
    bool IsWrapping;
    float Area;
    int MeasureCount;
    float LastAvailableWidth;
} SizedWidget;


/* A column that counts what it is asked. */
typedef struct CountingStackWidgetStruct
{
    Widget Base;
    UIStackLayout Layout;
    int MeasureCount;
} CountingStackWidget;


// Static functions.
static Error Sized_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    UNUSED(context);
    SizedWidget* Sized = self;
    Sized->MeasureCount++;
    Sized->LastAvailableWidth = availableWidth;
    *outSize = Sized->Preferred;
    if (Sized->IsWrapping && (availableWidth > 0.0f) && !isinf(availableWidth))
    {
        *outSize = (Vector2){ .x = availableWidth, .y = Sized->Area / availableWidth };
    }
    return Error_CreateSuccess();
}

static const WidgetVTable SizedVTable = { .Measure = Sized_Measure };

/* A stack that counts how often it is asked for its preferred size: a cost that comes back every tick shows here. */
static Error CountingStack_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    CountingStackWidget* Stack = self;
    Stack->MeasureCount++;
    return UIStackLayout_Measure(&Stack->Layout, &Stack->Base, context, availableWidth, outSize);
}

static Error CountingStack_Layout(void* self, const UILayoutContext* context)
{
    CountingStackWidget* Stack = self;
    return UIStackLayout_Arrange(&Stack->Layout, &Stack->Base, context);
}

static const WidgetVTable CountingStackVTable = { .Measure = CountingStack_Measure, .Layout = CountingStack_Layout };

/* A counting column filling its share of a row. */
static CountingStackWidget* AddCountingColumn(TestRun* run, TestUIFixture* fixture, uint64_t type, uint64_t parentId, UIStackHint hint)
{
    Widget* Created = NULL;
    Widget* Parent = NULL;
    if (!TEST_CHECK_SUCCESS(run, UIWidgetFactory_CreateWidget(&fixture->Widgets, &fixture->Screen, type, NULL, &Created))
        || !TEST_CHECK(run, UIScreen_TryGetWidgetById(&fixture->Screen, parentId, &Parent)))
    {
        return NULL;
    }
    CountingStackWidget* Stack = (CountingStackWidget*)Created;
    Stack->Layout = UIStackOptions_Create(UIStackDirection_Column).Layout;
    TEST_CHECK_SUCCESS(run, Widget_SetStackHint(Created, &hint));
    TEST_CHECK_SUCCESS(run, Widget_InitializeWidget(Created));
    TEST_CHECK_SUCCESS(run, Widget_AddSubWidget(Parent, Created));
    return Stack;
}

static uint64_t RegisterSizedType(TestRun* run, TestUIFixture* fixture)
{
    WidgetTypeDescriptor Descriptor = { .Name = u8"Sized", .StructSize = sizeof(SizedWidget), .VTable = &SizedVTable };
    uint64_t Type = WIDGET_TYPE_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIWidgetFactory_RegisterType(&fixture->Widgets, &Descriptor, &Type));
    return Type;
}

/* A sized widget in @p parentId (a root when it is 0), with a hint. */
static SizedWidget* AddSized(TestRun* run, TestUIFixture* fixture, uint64_t type, uint64_t parentId, Vector2 preferred, UIStackHint hint)
{
    Widget* Created = NULL;
    if (!TEST_CHECK_SUCCESS(run, UIWidgetFactory_CreateWidget(&fixture->Widgets, &fixture->Screen, type, NULL, &Created)))
    {
        return NULL;
    }
    SizedWidget* Sized = (SizedWidget*)Created;
    Sized->Preferred = preferred;
    TEST_CHECK_SUCCESS(run, Widget_SetStackHint(Created, &hint));
    TEST_CHECK_SUCCESS(run, Widget_InitializeWidget(Created));
    Widget* Parent = NULL;
    if ((parentId != WIDGET_ID_INVALID) && TEST_CHECK(run, UIScreen_TryGetWidgetById(&fixture->Screen, parentId, &Parent)))
    {
        TEST_CHECK_SUCCESS(run, Widget_AddSubWidget(Parent, Created));
    }
    else
    {
        TEST_CHECK_SUCCESS(run, UIScreen_AddWidget(&fixture->Screen, Created));
    }
    return Sized;
}

/* A stack filling the surface, or filling its parent's box by its hint. */
static uint64_t AddStack(TestRun* run, TestUIFixture* fixture, uint64_t parentId, UIStackDirection direction, UIInsets padding, float spacing)
{
    UIStackOptions Options = UIStackOptions_Create(direction);
    Options.Layout.Padding = padding;
    Options.Layout.Spacing = spacing;
    if (parentId == WIDGET_ID_INVALID)
    {
        Options.Element.HasPlacement = true;
        Options.Element.Placement = UIPlacement_Stretch(UIInsets_All(0.0f));
    }
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&fixture->Kit, &fixture->Screen, parentId, &Options, &Id));
    return Id;
}

static uint64_t IdOf(const SizedWidget* widget)
{
    return (widget != NULL) ? Widget_GetId(&widget->Base) : WIDGET_ID_INVALID;
}

/* A kit label in @p parentId with a stack's stretch and wrapping. */
static uint64_t AddWrappedLabel(TestRun* run, TestUIFixture* fixture, uint64_t parentId, const unsigned char* text)
{
    UILabelOptions Options = UILabelOptions_Create(text);
    Options.IsWrapped = true;
    Options.Element.Hint.Align = UIStackAlign_Stretch;
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIKit_CreateLabel(&fixture->Kit, &fixture->Screen, parentId, &Options, &Id));
    return Id;
}

/* A placement's box in pixels, for a parent of the whole surface. */
static Rectangle ResolveInSurface(const UIPlacement* placement, Vector2 surface, float scale)
{
    Vector2 Position = { 0 };
    Vector2 Size = { 0 };
    UIPlacement_Resolve(placement, (Vector2){ .x = 0.0f, .y = 0.0f }, (Vector2){ .x = 1.0f, .y = 1.0f }, surface, scale, &Position, &Size);
    return (Rectangle){ .x = Position.x * surface.x, .y = Position.y * surface.y, .width = Size.x * surface.x, .height = Size.y * surface.y };
}


// Tests: the pure arithmetic.
static void PlacementResolvesAnchorsAndOffsets(TestRun* run)
{
    Vector2 Surface = { .x = 1000.0f, .y = 500.0f };
    UIPlacement TopLeft = UIPlacement_FromTopLeft(10.0f, 20.0f, 100.0f, 30.0f);
    TEST_CHECK(run, TestUI_IsBox(ResolveInSurface(&TopLeft, Surface, 1.0f), 10.0f, 20.0f, 100.0f, 30.0f));

    UIPlacement Stretch = UIPlacement_Stretch((UIInsets){ .Left = 1.0f, .Top = 2.0f, .Right = 3.0f, .Bottom = 4.0f });
    TEST_CHECK(run, TestUI_IsBox(ResolveInSurface(&Stretch, Surface, 1.0f), 1.0f, 2.0f, 996.0f, 494.0f));

    // A box of a fixed size at each corner and the centre, by its pivot.
    UIPlacement Centre = UIPlacement_Fixed((Vector2){ .x = 0.5f, .y = 0.5f }, (Vector2){ .x = 0.5f, .y = 0.5f }, (Vector2){ .x = 0.0f, .y = 0.0f },
        (Vector2){ .x = 100.0f, .y = 50.0f });
    TEST_CHECK(run, TestUI_IsBox(ResolveInSurface(&Centre, Surface, 1.0f), 450.0f, 225.0f, 100.0f, 50.0f));
    UIPlacement BottomRight = UIPlacement_Fixed((Vector2){ .x = 1.0f, .y = 1.0f }, (Vector2){ .x = 1.0f, .y = 1.0f }, (Vector2){ .x = -5.0f, .y = -6.0f },
        (Vector2){ .x = 100.0f, .y = 50.0f });
    TEST_CHECK(run, TestUI_IsBox(ResolveInSurface(&BottomRight, Surface, 1.0f), 895.0f, 444.0f, 100.0f, 50.0f));

    // Anchors apart on one axis, together on the other: a bar across the top.
    UIPlacement Bar = { .AnchorMin = { .x = 0.0f, .y = 0.0f }, .AnchorMax = { .x = 1.0f, .y = 0.0f },
        .OffsetMin = { .x = 0.0f, .y = 0.0f }, .OffsetMax = { .x = 0.0f, .y = 24.0f } };
    TEST_CHECK(run, TestUI_IsBox(ResolveInSurface(&Bar, Surface, 1.0f), 0.0f, 0.0f, 1000.0f, 24.0f));
}

static void PlacementScalesOffsetsAndRoundsToWholePixels(TestRun* run)
{
    Vector2 Surface = { .x = 1000.0f, .y = 500.0f };
    UIPlacement Box = UIPlacement_FromTopLeft(10.0f, 20.0f, 100.0f, 30.0f);
    TEST_CHECK(run, TestUI_IsBox(ResolveInSurface(&Box, Surface, 2.0f), 20.0f, 40.0f, 200.0f, 60.0f));

    // 1.5 scale: the edges at 15, 30, 165 and 75 are whole already; 1 UI pixel rounds up to 2.
    TEST_CHECK(run, TestUI_IsBox(ResolveInSurface(&Box, Surface, 1.5f), 15.0f, 30.0f, 150.0f, 45.0f));
    UIPlacement Thin = UIPlacement_FromTopLeft(1.0f, 1.0f, 1.0f, 1.0f);
    Rectangle ThinBox = ResolveInSurface(&Thin, Surface, 1.5f);
    TEST_CHECK(run, TestUI_IsBox(ThinBox, 2.0f, 2.0f, 1.0f, 1.0f)); // 1.5 rounds to 2, 3.0 stays 3

    // Every edge is whole at scales that are not.
    const float Scales[] = { 1.25f, 1.37f, 0.75f, 1.9f };
    for (size_t Index = 0U; Index < (sizeof(Scales) / sizeof(Scales[0])); Index++)
    {
        UIPlacement Any = UIPlacement_FromTopLeft(13.3f, 7.7f, 61.9f, 23.1f);
        Rectangle Resolved = ResolveInSurface(&Any, Surface, Scales[Index]);
        TEST_CHECK(run, TestUI_IsSamePixel(Resolved.x, roundf(Resolved.x)) && TestUI_IsSamePixel(Resolved.y, roundf(Resolved.y))
            && TestUI_IsSamePixel(Resolved.x + Resolved.width, roundf(Resolved.x + Resolved.width))
            && TestUI_IsSamePixel(Resolved.y + Resolved.height, roundf(Resolved.y + Resolved.height)));
    }
}

/* Two boxes sharing an edge in UI pixels share it on the surface at any scale: no gap, no overlap. */
static void NeighboursShareTheirEdgeAtAnyScale(TestRun* run)
{
    Vector2 Surface = { .x = 1920.0f, .y = 1080.0f };
    for (float Scale = 0.25f; Scale < 3.0f; Scale += 0.0173f)
    {
        UIPlacement Left = UIPlacement_FromTopLeft(3.3f, 0.0f, 17.7f, 10.0f);
        UIPlacement Right = UIPlacement_FromTopLeft(21.0f, 0.0f, 9.0f, 10.0f);
        Rectangle LeftBox = ResolveInSurface(&Left, Surface, Scale);
        Rectangle RightBox = ResolveInSurface(&Right, Surface, Scale);
        TEST_CHECK(run, TestUI_IsSamePixel(LeftBox.x + LeftBox.width, RightBox.x));
    }
}

static void PlacementsOfNoSizeGiveZeroNeverNaN(TestRun* run)
{
    UIPlacement Box = UIPlacement_FromTopLeft(10.0f, 20.0f, 100.0f, 30.0f);
    Vector2 Position = { .x = 5.0f, .y = 5.0f };
    Vector2 Size = { .x = 5.0f, .y = 5.0f };

    // A parent of no size on one axis gives zero on that axis alone.
    UIPlacement_Resolve(&Box, (Vector2){ .x = 0.0f, .y = 0.0f }, (Vector2){ .x = 0.0f, .y = 1.0f }, (Vector2){ .x = 1000.0f, .y = 500.0f }, 1.0f, &Position, &Size);
    TEST_CHECK(run, (Position.x == 0.0f) && (Size.x == 0.0f) && (Position.y > 0.0f) && (Size.y > 0.0f));

    const float Hostile[] = { NAN, INFINITY, -INFINITY, 0.0f, -1.0f };
    for (size_t Index = 0U; Index < (sizeof(Hostile) / sizeof(Hostile[0])); Index++)
    {
        UIPlacement_Resolve(&Box, (Vector2){ .x = 0.0f, .y = 0.0f }, (Vector2){ .x = 1.0f, .y = 1.0f }, (Vector2){ .x = 1000.0f, .y = 500.0f }, Hostile[Index], &Position, &Size);
        TEST_CHECK(run, (Position.x == 0.0f) && (Size.x == 0.0f) && (Position.y == 0.0f) && (Size.y == 0.0f));
        UIPlacement_Resolve(&Box, (Vector2){ .x = 0.0f, .y = 0.0f }, (Vector2){ .x = 1.0f, .y = 1.0f }, (Vector2){ .x = Hostile[Index], .y = 500.0f }, 1.0f, &Position, &Size);
        TEST_CHECK(run, (Position.x == 0.0f) && (Size.x == 0.0f) && (Position.y > 0.0f) && (Size.y > 0.0f));
        UIPlacement_Resolve(&Box, (Vector2){ .x = Hostile[Index], .y = 0.0f }, (Vector2){ .x = 1.0f, .y = 1.0f }, (Vector2){ .x = 1000.0f, .y = 500.0f }, 1.0f, &Position, &Size);
        bool IsFiniteOnX = isfinite(Position.x) && isfinite(Size.x);
        TEST_CHECK(run, IsFiniteOnX);
    }
}

static void ABoxTurnedInsideOutHasNoSize(TestRun* run)
{
    Vector2 Surface = { .x = 1000.0f, .y = 500.0f };
    UIPlacement Inverted = UIPlacement_Stretch(UIInsets_All(600.0f));
    Rectangle Box = ResolveInSurface(&Inverted, Surface, 1.0f);
    TEST_CHECK(run, TestUI_IsBox(Box, 600.0f, 600.0f, 0.0f, 0.0f));
}

static void PlacementsAndHintsThatAreNotValidAreRefused(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterSizedType(run, &Fixture);
        SizedWidget* Sized = AddSized(run, &Fixture, Type, WIDGET_ID_INVALID, (Vector2){ .x = 1.0f, .y = 1.0f }, UIStackHint_Content());
        if (Sized != NULL)
        {
            UIPlacement Bad = UIPlacement_FromTopLeft(0.0f, 0.0f, 1.0f, 1.0f);
            Bad.AnchorMin.x = 1.5f;
            TEST_CHECK_ERROR(run, Widget_SetPlacement(&Sized->Base, &Bad), ErrorCode_ArgumentOutOfRange);
            Bad = UIPlacement_FromTopLeft(0.0f, 0.0f, 1.0f, 1.0f);
            Bad.AnchorMax.y = -0.1f;
            TEST_CHECK_ERROR(run, Widget_SetPlacement(&Sized->Base, &Bad), ErrorCode_ArgumentOutOfRange);
            Bad = UIPlacement_FromTopLeft(NAN, 0.0f, 1.0f, 1.0f);
            TEST_CHECK_ERROR(run, Widget_SetPlacement(&Sized->Base, &Bad), ErrorCode_ArgumentOutOfRange);
            Bad = UIPlacement_FromTopLeft(0.0f, 0.0f, INFINITY, 1.0f);
            TEST_CHECK_ERROR(run, Widget_SetPlacement(&Sized->Base, &Bad), ErrorCode_ArgumentOutOfRange);
            Bad = UIPlacement_FromTopLeft(0.0f, 0.0f, 1.0f, 1.0f);
            Bad.OffsetMin.x = NAN;
            TEST_CHECK_ERROR(run, Widget_SetPlacement(&Sized->Base, &Bad), ErrorCode_ArgumentOutOfRange);
            Bad = UIPlacement_FromTopLeft(0.0f, 0.0f, 1.0f, 1.0f);
            Bad.OffsetMax.y = INFINITY;
            TEST_CHECK_ERROR(run, Widget_SetPlacement(&Sized->Base, &Bad), ErrorCode_ArgumentOutOfRange);
            TEST_CHECK(run, !Widget_HasPlacement(&Sized->Base));

            UIStackHint Hint = UIStackHint_Fixed(-1.0f);
            TEST_CHECK_ERROR(run, Widget_SetStackHint(&Sized->Base, &Hint), ErrorCode_ArgumentOutOfRange);
            Hint = UIStackHint_Fill(0.0f);
            TEST_CHECK_ERROR(run, Widget_SetStackHint(&Sized->Base, &Hint), ErrorCode_ArgumentOutOfRange);
            Hint = UIStackHint_Fill(NAN);
            TEST_CHECK_ERROR(run, Widget_SetStackHint(&Sized->Base, &Hint), ErrorCode_ArgumentOutOfRange);
            Hint = UIStackHint_Content();
            Hint.Mode = (UIStackSizeMode)99;
            TEST_CHECK_ERROR(run, Widget_SetStackHint(&Sized->Base, &Hint), ErrorCode_ArgumentOutOfRange);
            Hint = UIStackHint_Content();
            Hint.Align = (UIStackAlign)99;
            TEST_CHECK_ERROR(run, Widget_SetStackHint(&Sized->Base, &Hint), ErrorCode_ArgumentOutOfRange);
            Hint = UIStackHint_Content();
            Hint.CrossSize = -1.0f;
            TEST_CHECK_ERROR(run, Widget_SetStackHint(&Sized->Base, &Hint), ErrorCode_ArgumentOutOfRange);
        }
        TEST_CHECK_ERROR(run, UIScreen_SetScale(&Fixture.Screen, NAN), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIScreen_SetScale(&Fixture.Screen, 0.0f), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIScreen_SetScale(&Fixture.Screen, 9.0f), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIScreen_SetScale(NULL, 1.0f), ErrorCode_IllegalArgument);
        TEST_CHECK(run, UIScreen_GetScale(&Fixture.Screen) == 1.0f);
    }
    TestUI_Deconstruct(run, &Fixture);
}


/* A box a whole number of pixels long keeps that length wherever it lies, also where its edges land on a half: what its text was measured to fit. */
static void ABoxOfWholePixelsKeepsItsLengthWhereverItLies(TestRun* run)
{
    const float Scales[] = { 1.0f, 1.25f, 1.5f, 2.0f, 3.0f };
    const float Lengths[] = { 7.0f, 95.0f, 116.0f, 777.0f };
    Vector2 Surface = { .x = 4096.0f, .y = 4096.0f };
    Vector2 Origin = { .x = 0.0f, .y = 0.0f };
    Vector2 Whole = { .x = 1.0f, .y = 1.0f };
    bool IsEveryLengthKept = true;
    for (size_t ScaleIndex = 0U; ScaleIndex < (sizeof(Scales) / sizeof(Scales[0])); ScaleIndex++)
    {
        float Scale = Scales[ScaleIndex];
        for (size_t LengthIndex = 0U; LengthIndex < (sizeof(Lengths) / sizeof(Lengths[0])); LengthIndex++)
        {
            float Width = Lengths[LengthIndex] / Scale;
            for (int Half = 0; Half < 4000; Half++)
            {
                float Start = ((float)Half * 0.5f) / Scale;
                UIPlacement Placement = UIPlacement_FromTopLeft(Start, 0.0f, Width, 1.0f);
                Vector2 Position = { .x = 0.0f, .y = 0.0f };
                Vector2 Size = { .x = 0.0f, .y = 0.0f };
                UIPlacement_Resolve(&Placement, Origin, Whole, Surface, Scale, &Position, &Size);
                IsEveryLengthKept = IsEveryLengthKept && TestUI_IsSamePixel(Size.x * Surface.x, Lengths[LengthIndex]);
            }
        }
    }
    TEST_CHECK(run, IsEveryLengthKept);
}

/* Numbers too large to add up are refused where they are given, so they never become a frame's error. */
static void LengthsTooLongToAddUpAreRefused(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterSizedType(run, &Fixture);
        SizedWidget* Sized = AddSized(run, &Fixture, Type, WIDGET_ID_INVALID, (Vector2){ .x = 1.0f, .y = 1.0f }, UIStackHint_Content());
        if (Sized != NULL)
        {
            UIStackHint Hint = UIStackHint_Fixed(FLT_MAX);
            TEST_CHECK_ERROR(run, Widget_SetStackHint(&Sized->Base, &Hint), ErrorCode_ArgumentOutOfRange);
            Hint = UIStackHint_Fill(FLT_MAX);
            TEST_CHECK_ERROR(run, Widget_SetStackHint(&Sized->Base, &Hint), ErrorCode_ArgumentOutOfRange);
            Hint = UIStackHint_Content();
            Hint.CrossSize = FLT_MAX;
            TEST_CHECK_ERROR(run, Widget_SetStackHint(&Sized->Base, &Hint), ErrorCode_ArgumentOutOfRange);
            Hint = UIStackHint_Fixed(UI_LAYOUT_MAX_LENGTH);
            TEST_CHECK_SUCCESS(run, Widget_SetStackHint(&Sized->Base, &Hint));

            UIPlacement Far = UIPlacement_FromTopLeft(3e38f, 0.0f, 1.0f, 1.0f);
            TEST_CHECK_ERROR(run, Widget_SetPlacement(&Sized->Base, &Far), ErrorCode_ArgumentOutOfRange);
            Far = UIPlacement_FromTopLeft(0.0f, -3e38f, 1.0f, 1.0f);
            TEST_CHECK_ERROR(run, Widget_SetPlacement(&Sized->Base, &Far), ErrorCode_ArgumentOutOfRange);
        }
        UIStackOptions Options = UIStackOptions_Create(UIStackDirection_Row);
        Options.Layout.Spacing = FLT_MAX;
        uint64_t Id = WIDGET_ID_INVALID;
        TEST_CHECK_ERROR(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Id), ErrorCode_ArgumentOutOfRange);
        Options = UIStackOptions_Create(UIStackDirection_Row);
        Options.Layout.Padding.Right = FLT_MAX;
        TEST_CHECK_ERROR(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Id), ErrorCode_ArgumentOutOfRange);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* The longest lengths a stack takes, many of them, and a preferred size past them: it all adds up and the frame goes on. */
static void TheLongestLengthsStillAddUp(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterSizedType(run, &Fixture);
        UIStackOptions Options = UIStackOptions_Create(UIStackDirection_Row);
        Options.Element.HasPlacement = true;
        Options.Element.Placement = UIPlacement_Stretch(UIInsets_All(0.0f));
        Options.Layout.Spacing = UI_LAYOUT_MAX_LENGTH;
        Options.Layout.Padding = UIInsets_All(UI_LAYOUT_MAX_LENGTH);
        uint64_t Row = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Row));
        SizedWidget* Last = NULL;
        for (int Index = 0; Index < 100; Index++)
        {
            UIStackHint Hint = ((Index % 2) == 0) ? UIStackHint_Fixed(UI_LAYOUT_MAX_LENGTH) : UIStackHint_Fill(UI_LAYOUT_MAX_LENGTH);
            Last = AddSized(run, &Fixture, Type, Row, (Vector2){ .x = FLT_MAX, .y = FLT_MAX }, Hint);
        }
        TestUI_Ticks(run, &Fixture, 3U);

        // A stack of a size set by hand, past anything: its children get what it can share out.
        Widget* Base = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Row, &Base)))
        {
            TEST_CHECK_SUCCESS(run, Widget_ClearPlacement(Base));
            TEST_CHECK_SUCCESS(run, Widget_SetSize(Base, (Vector2){ .x = FLT_MAX, .y = FLT_MAX }));
            TestUI_Ticks(run, &Fixture, 3U);
        }

        // A preferred size past the limit is kept to it.
        if (Last != NULL)
        {
            RenderContext Measure;
            RenderContext_CreateMeasuring(&Measure, (Vector2){ .x = 1000.0f, .y = 500.0f }, 2.0f, RenderTargetPosition_Centered());
            UIMeasureContext Context = { .Measure = &Measure };
            Vector2 Size = { .x = 0.0f, .y = 0.0f };
            TEST_CHECK_SUCCESS(run, Widget_Measure(&Last->Base, &Context, 100.0f, &Size));
            TEST_CHECK(run, (Size.x == UI_LAYOUT_MAX_LENGTH) && (Size.y == UI_LAYOUT_MAX_LENGTH));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

// Tests: on a screen.
static void APlacedWidgetFollowsTheSurfaceAndTheScale(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterSizedType(run, &Fixture);
        SizedWidget* Sized = AddSized(run, &Fixture, Type, WIDGET_ID_INVALID, (Vector2){ .x = 1.0f, .y = 1.0f }, UIStackHint_Content());
        uint64_t Id = IdOf(Sized);
        UIPlacement Corner = UIPlacement_Fixed((Vector2){ .x = 1.0f, .y = 0.0f }, (Vector2){ .x = 1.0f, .y = 0.0f }, (Vector2){ .x = -10.0f, .y = 10.0f },
            (Vector2){ .x = 100.0f, .y = 24.0f });
        if ((Sized != NULL) && TEST_CHECK_SUCCESS(run, Widget_SetPlacement(&Sized->Base, &Corner)))
        {
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Id), 890.0f, 10.0f, 100.0f, 24.0f));

            // A surface of another size: the corner follows, the size stays in pixels.
            TestUI_SetSurface(run, &Fixture, 1600.0f, 900.0f);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Id), 1490.0f, 10.0f, 100.0f, 24.0f));

            TEST_CHECK_SUCCESS(run, UIScreen_SetScale(&Fixture.Screen, 2.0f));
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Id), 1380.0f, 20.0f, 200.0f, 48.0f));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Deep trees stay on whole pixels: each level's float noise does not build up. */
static void NestedPlacementsLandOnWholePixelsAtAnyDepth(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterSizedType(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIScreen_SetScale(&Fixture.Screen, 1.25f));
        TestUI_SetSurface(run, &Fixture, 1919.0f, 1079.0f);
        uint64_t ParentId = WIDGET_ID_INVALID;
        uint64_t Ids[WIDGET_MAX_TREE_DEPTH];
        for (size_t Level = 0U; Level < WIDGET_MAX_TREE_DEPTH; Level++)
        {
            SizedWidget* Sized = AddSized(run, &Fixture, Type, ParentId, (Vector2){ .x = 1.0f, .y = 1.0f }, UIStackHint_Content());
            UIPlacement Inside = UIPlacement_Stretch((UIInsets){ .Left = 1.7f, .Top = 0.3f, .Right = 2.1f, .Bottom = 0.9f });
            TEST_CHECK(run, (Sized != NULL) && TEST_CHECK_SUCCESS(run, Widget_SetPlacement(&Sized->Base, &Inside)));
            Ids[Level] = IdOf(Sized);
            ParentId = Ids[Level];
        }
        TestUI_Tick(run, &Fixture);
        for (size_t Level = 0U; Level < WIDGET_MAX_TREE_DEPTH; Level++)
        {
            Rectangle Box = TestUI_GetPixelBox(run, &Fixture, Ids[Level]);
            TEST_CHECK(run, TestUI_IsSamePixel(Box.x, roundf(Box.x)) && TestUI_IsSamePixel(Box.y, roundf(Box.y))
                && TestUI_IsSamePixel(Box.x + Box.width, roundf(Box.x + Box.width))
                && TestUI_IsSamePixel(Box.y + Box.height, roundf(Box.y + Box.height)));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void WidgetsWithoutAPlacementDoNotChange(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterSizedType(run, &Fixture);
        SizedWidget* Sized = AddSized(run, &Fixture, Type, WIDGET_ID_INVALID, (Vector2){ .x = 1.0f, .y = 1.0f }, UIStackHint_Content());
        if (Sized != NULL)
        {
            Vector2 Position = { .x = 0.3123f, .y = 0.7f };
            Vector2 Size = { .x = 0.1f, .y = 0.2333f };
            TEST_CHECK_SUCCESS(run, Widget_SetPosition(&Sized->Base, Position));
            TEST_CHECK_SUCCESS(run, Widget_SetSize(&Sized->Base, Size));
            TEST_CHECK_SUCCESS(run, UIScreen_SetScale(&Fixture.Screen, 2.0f));
            TestUI_SetSurface(run, &Fixture, 777.0f, 333.0f);
            TestUI_Ticks(run, &Fixture, 3U);
            TEST_CHECK(run, (Widget_GetPosition(&Sized->Base).x == Position.x) && (Widget_GetPosition(&Sized->Base).y == Position.y)
                && (Widget_GetSize(&Sized->Base).x == Size.x) && (Widget_GetSize(&Sized->Base).y == Size.y));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ClearingAPlacementKeepsTheBox(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterSizedType(run, &Fixture);
        SizedWidget* Sized = AddSized(run, &Fixture, Type, WIDGET_ID_INVALID, (Vector2){ .x = 1.0f, .y = 1.0f }, UIStackHint_Content());
        UIPlacement Box = UIPlacement_FromTopLeft(10.0f, 20.0f, 100.0f, 30.0f);
        if ((Sized != NULL) && TEST_CHECK_SUCCESS(run, Widget_SetPlacement(&Sized->Base, &Box)))
        {
            TestUI_Tick(run, &Fixture);
            TEST_CHECK_SUCCESS(run, Widget_ClearPlacement(&Sized->Base));
            TEST_CHECK(run, !Widget_HasPlacement(&Sized->Base));
            TestUI_SetSurface(run, &Fixture, 2000.0f, 1000.0f);
            TestUI_Tick(run, &Fixture);
            // The normalized box stays: on a surface twice as large it is twice as large.
            TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, IdOf(Sized)), 20.0f, 40.0f, 200.0f, 60.0f));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* An animation of a placed widget's position or size changes nothing in the end: the placement wins. */
static void APlacementWinsOverAnAnimation(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterSizedType(run, &Fixture);
        SizedWidget* Sized = AddSized(run, &Fixture, Type, WIDGET_ID_INVALID, (Vector2){ .x = 1.0f, .y = 1.0f }, UIStackHint_Content());
        UIPlacement Box = UIPlacement_FromTopLeft(10.0f, 20.0f, 100.0f, 30.0f);
        if ((Sized != NULL) && TEST_CHECK_SUCCESS(run, Widget_SetPlacement(&Sized->Base, &Box)))
        {
            UIKeyframe Frames[] =
            {
                { .Time = 0.0, .Interpolation = UIInterpolation_Linear, .Value.Vector2Value = { .x = 0.9f, .y = 0.9f } },
                { .Time = 10.0, .Interpolation = UIInterpolation_Linear, .Value.Vector2Value = { .x = 0.8f, .y = 0.8f } },
            };
            uint64_t Animation = UI_SCREEN_ID_INVALID;
            TEST_CHECK_SUCCESS(run, UIScreen_StartAnimation(&Fixture.Screen, IdOf(Sized), WidgetBaseProperty_Position, UIPropertyType_Vector2,
                Frames, 2U, UIAnimationOptions_CreateDefault(), &Animation));
            TestUI_Ticks(run, &Fixture, 5U);
            TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, IdOf(Sized)), 10.0f, 20.0f, 100.0f, 30.0f));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* An idle placed widget writes nothing: the screen does not hit-test again every tick. */
static void AnIdlePlacementDoesNotHitTestAgain(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterSizedType(run, &Fixture);
        uint64_t Stack = AddStack(run, &Fixture, WIDGET_ID_INVALID, UIStackDirection_Column, UIInsets_All(3.0f), 2.0f);
        AddSized(run, &Fixture, Type, Stack, (Vector2){ .x = 10.0f, .y = 10.0f }, UIStackHint_Content());
        TestUI_Ticks(run, &Fixture, 3U);
        TEST_CHECK(run, !Fixture.Screen._isHitTestDirty);
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Tests: preferred sizes.
static void MeasuringIsCachedUntilSomethingChanges(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterSizedType(run, &Fixture);
        uint64_t Stack = AddStack(run, &Fixture, WIDGET_ID_INVALID, UIStackDirection_Column, UIInsets_All(0.0f), 0.0f);
        SizedWidget* Sized = AddSized(run, &Fixture, Type, Stack, (Vector2){ .x = 10.0f, .y = 20.0f }, UIStackHint_Content());
        if (Sized != NULL)
        {
            TestUI_Tick(run, &Fixture);
            int Count = Sized->MeasureCount;
            TEST_CHECK(run, Count == 1);
            TestUI_Ticks(run, &Fixture, 10U);
            TEST_CHECK(run, Sized->MeasureCount == Count);

            // What changes it, each once.
            Widget_InvalidateMeasure(&Sized->Base);
            TestUI_Ticks(run, &Fixture, 3U);
            TEST_CHECK(run, Sized->MeasureCount == (Count + 1));
            TEST_CHECK_SUCCESS(run, UIScreen_SetScale(&Fixture.Screen, 1.5f));
            TestUI_Ticks(run, &Fixture, 3U);
            TEST_CHECK(run, Sized->MeasureCount == (Count + 2));
            TestUI_SetSurface(run, &Fixture, 800.0f, 600.0f);
            TestUI_Ticks(run, &Fixture, 3U);
            TEST_CHECK(run, Sized->MeasureCount == (Count + 3));
            TEST_CHECK(run, TestUI_SetTheme(run, &Fixture, PLAIN_THEME));
            TestUI_Ticks(run, &Fixture, 3U);
            TEST_CHECK(run, Sized->MeasureCount == (Count + 4));

            // A scale and a viewport that are set to what they are, every tick by a host, change nothing.
            TEST_CHECK_SUCCESS(run, UIScreen_SetScale(&Fixture.Screen, 1.5f));
            TestUI_SetSurface(run, &Fixture, 800.0f, 600.0f);
            TestUI_Ticks(run, &Fixture, 3U);
            TEST_CHECK(run, Sized->MeasureCount == (Count + 4));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void AChangeBelowDropsTheCacheAllTheWayUp(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterSizedType(run, &Fixture);
        uint64_t Outer = AddStack(run, &Fixture, WIDGET_ID_INVALID, UIStackDirection_Row, UIInsets_All(0.0f), 0.0f);
        uint64_t Inner = AddStack(run, &Fixture, Outer, UIStackDirection_Column, UIInsets_All(0.0f), 0.0f);
        SizedWidget* Sized = AddSized(run, &Fixture, Type, Inner, (Vector2){ .x = 10.0f, .y = 20.0f }, UIStackHint_Content());
        if (Sized != NULL)
        {
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Inner), 0.0f, 0.0f, 10.0f, 500.0f));
            Sized->Preferred.x = 33.0f;
            TestUI_Tick(run, &Fixture); // nobody said so: cached
            TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Inner), 0.0f, 0.0f, 10.0f, 500.0f));
            Widget_InvalidateMeasure(&Sized->Base);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Inner), 0.0f, 0.0f, 33.0f, 500.0f));

            // Adding and removing a child drop it too.
            SizedWidget* Second = AddSized(run, &Fixture, Type, Inner, (Vector2){ .x = 50.0f, .y = 5.0f }, UIStackHint_Content());
            TestUI_Ticks(run, &Fixture, 2U);
            TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Inner), 0.0f, 0.0f, 50.0f, 500.0f));
            Widget* Parent = Widget_GetParent(&Second->Base);
            TEST_CHECK_SUCCESS(run, Widget_RemoveSubWidget(Parent, &Second->Base));
            TestUI_Ticks(run, &Fixture, 2U);
            TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Inner), 0.0f, 0.0f, 33.0f, 500.0f));
            TEST_CHECK_SUCCESS(run, Widget_Destroy(&Second->Base));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Scale and surface change what a text measures in UI pixels; a widget's own width may stay the same, so the generation has to say so. */
static void TheGenerationCountsAScaleAndASurfaceThatChange(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Generation = UIScreen_GetMeasureGeneration(&Fixture.Screen);
        TEST_CHECK_SUCCESS(run, UIScreen_SetScale(&Fixture.Screen, 1.5f));
        TEST_CHECK(run, UIScreen_GetMeasureGeneration(&Fixture.Screen) == (Generation + 1U));
        TEST_CHECK_SUCCESS(run, UIScreen_SetScale(&Fixture.Screen, 1.5f));
        TEST_CHECK(run, UIScreen_GetMeasureGeneration(&Fixture.Screen) == (Generation + 1U));
        TestUI_SetSurface(run, &Fixture, 800.0f, 600.0f);
        TEST_CHECK(run, UIScreen_GetMeasureGeneration(&Fixture.Screen) == (Generation + 2U));
        TestUI_SetSurface(run, &Fixture, 800.0f, 600.0f);
        TEST_CHECK(run, UIScreen_GetMeasureGeneration(&Fixture.Screen) == (Generation + 2U));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A child's hint is part of what the stack above answers. */
static void AChildsNewHintDropsTheCacheAbove(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterSizedType(run, &Fixture);
        uint64_t Outer = AddStack(run, &Fixture, WIDGET_ID_INVALID, UIStackDirection_Row, UIInsets_All(0.0f), 0.0f);
        uint64_t Inner = AddStack(run, &Fixture, Outer, UIStackDirection_Column, UIInsets_All(0.0f), 0.0f);
        SizedWidget* Sized = AddSized(run, &Fixture, Type, Inner, (Vector2){ .x = 10.0f, .y = 20.0f }, UIStackHint_Content());
        if (Sized != NULL)
        {
            TestUI_Ticks(run, &Fixture, 2U);
            TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Inner), 0.0f, 0.0f, 10.0f, 500.0f));
            UIStackHint Hint = UIStackHint_Content();
            Hint.Align = UIStackAlign_Start;
            Hint.HasCrossSize = true;
            Hint.CrossSize = 33.0f;
            TEST_CHECK_SUCCESS(run, StackWidget_SetChildHint(&Fixture.Screen, IdOf(Sized), &Hint));
            TestUI_Ticks(run, &Fixture, 2U);
            TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Inner), 0.0f, 0.0f, 33.0f, 500.0f));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void MeasureAnswersStayFiniteAndWidthsCompareWithoutNoise(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterSizedType(run, &Fixture);
        SizedWidget* Sized = AddSized(run, &Fixture, Type, WIDGET_ID_INVALID, (Vector2){ .x = NAN, .y = -5.0f }, UIStackHint_Content());
        if (Sized != NULL)
        {
            RenderContext Measure;
            RenderContext_CreateMeasuring(&Measure, (Vector2){ .x = 1000.0f, .y = 500.0f }, 2.0f, RenderTargetPosition_Centered());
            UIMeasureContext Context = { .Measure = &Measure };
            Vector2 Size = { .x = 9.0f, .y = 9.0f };
            TEST_CHECK_SUCCESS(run, Widget_Measure(&Sized->Base, &Context, 100.0f, &Size));
            TEST_CHECK(run, (Size.x == 0.0f) && (Size.y == 0.0f));
            Sized->Preferred = (Vector2){ .x = INFINITY, .y = 7.0f };
            Widget_InvalidateMeasure(&Sized->Base);
            TEST_CHECK_SUCCESS(run, Widget_Measure(&Sized->Base, &Context, 100.0f, &Size));
            TEST_CHECK(run, (Size.x == 0.0f) && (Size.y == 7.0f));

            // The same width with float noise answers from the cache; another width asks again.
            int Count = Sized->MeasureCount;
            TEST_CHECK_SUCCESS(run, Widget_Measure(&Sized->Base, &Context, 100.0f + 0.001f, &Size));
            TEST_CHECK(run, Sized->MeasureCount == Count);
            TEST_CHECK_SUCCESS(run, Widget_Measure(&Sized->Base, &Context, 101.0f, &Size));
            TEST_CHECK(run, Sized->MeasureCount == (Count + 1));

            // A NaN or a negative width is no width at all; an unbounded one stays unbounded.
            TEST_CHECK_SUCCESS(run, Widget_Measure(&Sized->Base, &Context, NAN, &Size));
            TEST_CHECK(run, Sized->LastAvailableWidth == 0.0f);
            Widget_InvalidateMeasure(&Sized->Base);
            TEST_CHECK_SUCCESS(run, Widget_Measure(&Sized->Base, &Context, -3.0f, &Size));
            TEST_CHECK(run, Sized->LastAvailableWidth == 0.0f);
            TEST_CHECK_SUCCESS(run, Widget_Measure(&Sized->Base, &Context, UI_MEASURE_UNBOUNDED_WIDTH, &Size));
            TEST_CHECK(run, isinf(Sized->LastAvailableWidth));
            TEST_CHECK_ERROR(run, Widget_Measure(NULL, &Context, 1.0f, &Size), ErrorCode_IllegalArgument);
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Tests: stacks.
static void AColumnSizesEachModeWithPaddingAndSpacing(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterSizedType(run, &Fixture);
        UIInsets Padding = { .Left = 5.0f, .Top = 6.0f, .Right = 7.0f, .Bottom = 8.0f };
        uint64_t Stack = AddStack(run, &Fixture, WIDGET_ID_INVALID, UIStackDirection_Column, Padding, 4.0f);
        uint64_t Content = IdOf(AddSized(run, &Fixture, Type, Stack, (Vector2){ .x = 50.0f, .y = 20.0f }, UIStackHint_Content()));
        uint64_t Fixed = IdOf(AddSized(run, &Fixture, Type, Stack, (Vector2){ .x = 50.0f, .y = 999.0f }, UIStackHint_Fixed(30.0f)));
        uint64_t FillA = IdOf(AddSized(run, &Fixture, Type, Stack, (Vector2){ .x = 50.0f, .y = 1.0f }, UIStackHint_Fill(1.0f)));
        uint64_t FillB = IdOf(AddSized(run, &Fixture, Type, Stack, (Vector2){ .x = 50.0f, .y = 1.0f }, UIStackHint_Fill(3.0f)));
        TestUI_Tick(run, &Fixture); // the stack's children are placed by the stack's layout
        TestUI_Tick(run, &Fixture);

        // The content box is 988 x 486: 20 + 30 + 3 gaps of 4 leave 424 for the fills, 106 and 318.
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Content), 5.0f, 6.0f, 988.0f, 20.0f));
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Fixed), 5.0f, 30.0f, 988.0f, 30.0f));
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, FillA), 5.0f, 64.0f, 988.0f, 106.0f));
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, FillB), 5.0f, 174.0f, 988.0f, 318.0f));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ARowSizesEachModeWithPaddingAndSpacing(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterSizedType(run, &Fixture);
        UIInsets Padding = { .Left = 5.0f, .Top = 6.0f, .Right = 7.0f, .Bottom = 8.0f };
        uint64_t Stack = AddStack(run, &Fixture, WIDGET_ID_INVALID, UIStackDirection_Row, Padding, 4.0f);
        uint64_t Content = IdOf(AddSized(run, &Fixture, Type, Stack, (Vector2){ .x = 50.0f, .y = 20.0f }, UIStackHint_Content()));
        uint64_t Fixed = IdOf(AddSized(run, &Fixture, Type, Stack, (Vector2){ .x = 999.0f, .y = 20.0f }, UIStackHint_Fixed(30.0f)));
        uint64_t FillA = IdOf(AddSized(run, &Fixture, Type, Stack, (Vector2){ .x = 1.0f, .y = 20.0f }, UIStackHint_Fill(1.0f)));
        uint64_t FillB = IdOf(AddSized(run, &Fixture, Type, Stack, (Vector2){ .x = 1.0f, .y = 20.0f }, UIStackHint_Fill(1.0f)));
        TestUI_Ticks(run, &Fixture, 2U);

        // The content box is 988 x 486: 50 + 30 + 3 gaps of 4 leave 896 for the fills, 448 each.
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Content), 5.0f, 6.0f, 50.0f, 486.0f));
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Fixed), 59.0f, 6.0f, 30.0f, 486.0f));
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, FillA), 93.0f, 6.0f, 448.0f, 486.0f));
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, FillB), 545.0f, 6.0f, 448.0f, 486.0f));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ChildrenAlignAcrossTheStack(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterSizedType(run, &Fixture);
        uint64_t Stack = AddStack(run, &Fixture, WIDGET_ID_INVALID, UIStackDirection_Column, UIInsets_All(10.0f), 0.0f);
        UIStackHint Hint = UIStackHint_Fixed(10.0f);
        Hint.Align = UIStackAlign_Start;
        uint64_t Start = IdOf(AddSized(run, &Fixture, Type, Stack, (Vector2){ .x = 100.0f, .y = 1.0f }, Hint));
        Hint.Align = UIStackAlign_Center;
        uint64_t Centre = IdOf(AddSized(run, &Fixture, Type, Stack, (Vector2){ .x = 100.0f, .y = 1.0f }, Hint));
        Hint.Align = UIStackAlign_End;
        uint64_t End = IdOf(AddSized(run, &Fixture, Type, Stack, (Vector2){ .x = 100.0f, .y = 1.0f }, Hint));
        Hint.Align = UIStackAlign_Stretch;
        uint64_t Stretch = IdOf(AddSized(run, &Fixture, Type, Stack, (Vector2){ .x = 100.0f, .y = 1.0f }, Hint));
        Hint.Align = UIStackAlign_Start;
        Hint.HasCrossSize = true;
        Hint.CrossSize = 77.0f;
        uint64_t Given = IdOf(AddSized(run, &Fixture, Type, Stack, (Vector2){ .x = 100.0f, .y = 1.0f }, Hint));
        Hint.Align = UIStackAlign_Stretch;
        uint64_t StretchOverGiven = IdOf(AddSized(run, &Fixture, Type, Stack, (Vector2){ .x = 100.0f, .y = 1.0f }, Hint));
        TestUI_Ticks(run, &Fixture, 2U);

        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Start), 10.0f, 10.0f, 100.0f, 10.0f));
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Centre), 450.0f, 20.0f, 100.0f, 10.0f));
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, End), 890.0f, 30.0f, 100.0f, 10.0f));
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Stretch), 10.0f, 40.0f, 980.0f, 10.0f));
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Given), 10.0f, 50.0f, 77.0f, 10.0f));
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, StretchOverGiven), 10.0f, 60.0f, 980.0f, 10.0f));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void HiddenChildrenTakeNoRoom(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterSizedType(run, &Fixture);
        uint64_t Outer = AddStack(run, &Fixture, WIDGET_ID_INVALID, UIStackDirection_Column, UIInsets_All(0.0f), 0.0f);
        uint64_t Stack = AddStack(run, &Fixture, Outer, UIStackDirection_Row, UIInsets_All(0.0f), 10.0f);
        SizedWidget* First = AddSized(run, &Fixture, Type, Stack, (Vector2){ .x = 50.0f, .y = 20.0f }, UIStackHint_Content());
        SizedWidget* Hidden = AddSized(run, &Fixture, Type, Stack, (Vector2){ .x = 70.0f, .y = 90.0f }, UIStackHint_Content());
        SizedWidget* Last = AddSized(run, &Fixture, Type, Stack, (Vector2){ .x = 30.0f, .y = 20.0f }, UIStackHint_Content());
        if ((First != NULL) && (Hidden != NULL) && (Last != NULL))
        {
            TestUI_Ticks(run, &Fixture, 3U);
            TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, IdOf(Last)), 140.0f, 0.0f, 30.0f, 90.0f));
            TEST_CHECK(run, TestUI_GetPixelBox(run, &Fixture, Stack).height == 90.0f);

            TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(&Hidden->Base, false));
            TestUI_Ticks(run, &Fixture, 3U);
            TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, IdOf(Last)), 60.0f, 0.0f, 30.0f, 20.0f));
            Rectangle HiddenBox = TestUI_GetPixelBox(run, &Fixture, IdOf(Hidden));
            TEST_CHECK(run, (HiddenBox.width == 0.0f) && (HiddenBox.height == 0.0f));
            // Its height no longer counts in the stack's own preferred size.
            TEST_CHECK(run, TestUI_IsSamePixel(TestUI_GetPixelBox(run, &Fixture, Stack).height, 20.0f));

            TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(&Hidden->Base, true));
            TestUI_Ticks(run, &Fixture, 3U);
            TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, IdOf(Last)), 140.0f, 0.0f, 30.0f, 90.0f));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A hidden child that fills takes neither a share nor a gap. */
static void AHiddenFillChildTakesNoShare(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterSizedType(run, &Fixture);
        uint64_t Stack = AddStack(run, &Fixture, WIDGET_ID_INVALID, UIStackDirection_Row, UIInsets_All(0.0f), 10.0f);
        AddSized(run, &Fixture, Type, Stack, (Vector2){ .x = 1.0f, .y = 1.0f }, UIStackHint_Fixed(100.0f));
        SizedWidget* Hidden = AddSized(run, &Fixture, Type, Stack, (Vector2){ .x = 1.0f, .y = 1.0f }, UIStackHint_Fill(1.0f));
        SizedWidget* Last = AddSized(run, &Fixture, Type, Stack, (Vector2){ .x = 1.0f, .y = 1.0f }, UIStackHint_Fill(1.0f));
        if ((Hidden != NULL) && (Last != NULL))
        {
            TestUI_Ticks(run, &Fixture, 2U);
            TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, IdOf(Last)), 560.0f, 0.0f, 440.0f, 500.0f));
            TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(&Hidden->Base, false));
            TestUI_Ticks(run, &Fixture, 2U);
            TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, IdOf(Last)), 110.0f, 0.0f, 890.0f, 500.0f));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Hidden with what it holds: nothing of it is hit, also where a box of no size lies exactly under the pointer. */
static void AHiddenWidgetIsNotThereForThePointer(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterSizedType(run, &Fixture);
        uint64_t Stack = AddStack(run, &Fixture, WIDGET_ID_INVALID, UIStackDirection_Column, UIInsets_All(0.0f), 0.0f);
        UIPanelOptions Options = UIPanelOptions_Create();
        Options.HasPadding = true;
        uint64_t Panel = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreatePanel(&Fixture.Kit, &Fixture.Screen, Stack, &Options, &Panel));
        SizedWidget* Inside = AddSized(run, &Fixture, Type, Panel, (Vector2){ .x = 100.0f, .y = 100.0f }, UIStackHint_Content());
        TestUI_Ticks(run, &Fixture, 3U);

        Widget* Top = NULL;
        Vector2 Inner = { .x = 0.01f, .y = 0.01f };
        TEST_CHECK(run, UIScreen_TryGetTopmostAt(&Fixture.Screen, Inner, &Top) && (Top == &Inside->Base));

        Widget* PanelWidget = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Panel, &PanelWidget)))
        {
            TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(PanelWidget, false));
            TEST_CHECK(run, !UIScreen_TryGetTopmostAt(&Fixture.Screen, Inner, &Top));
            TestUI_Ticks(run, &Fixture, 3U);
            TEST_CHECK(run, !UIScreen_TryGetTopmostAt(&Fixture.Screen, Inner, &Top));
            TEST_CHECK(run, !UIScreen_TryGetTopmostAt(&Fixture.Screen, (Vector2){ .x = 0.0f, .y = 0.0f }, &Top));

            // The hover moves on at once.
            TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(PanelWidget, true));
            TestUI_Ticks(run, &Fixture, 3U);
            TestUI_MovePointer(&Fixture, 10.0f, 10.0f);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, UIScreen_GetHoveredWidgetId(&Fixture.Screen) == IdOf(Inside));
            TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(PanelWidget, false));
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, UIScreen_GetHoveredWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID);
        }

        // A widget of no size holds no point, even its own corner.
        SizedWidget* Empty = AddSized(run, &Fixture, Type, WIDGET_ID_INVALID, (Vector2){ .x = 0.0f, .y = 0.0f }, UIStackHint_Content());
        TEST_CHECK_SUCCESS(run, Widget_SetPosition(&Empty->Base, (Vector2){ .x = 0.5f, .y = 0.5f }));
        TEST_CHECK(run, !UIScreen_TryGetTopmostAt(&Fixture.Screen, (Vector2){ .x = 0.5f, .y = 0.5f }, &Top));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void OverflowIsLaidOutAsItIs(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterSizedType(run, &Fixture);
        uint64_t Stack = AddStack(run, &Fixture, WIDGET_ID_INVALID, UIStackDirection_Column, UIInsets_All(0.0f), 0.0f);
        uint64_t Big = IdOf(AddSized(run, &Fixture, Type, Stack, (Vector2){ .x = 10.0f, .y = 600.0f }, UIStackHint_Content()));
        uint64_t Fill = IdOf(AddSized(run, &Fixture, Type, Stack, (Vector2){ .x = 10.0f, .y = 5.0f }, UIStackHint_Fill(1.0f)));
        uint64_t After = IdOf(AddSized(run, &Fixture, Type, Stack, (Vector2){ .x = 10.0f, .y = 30.0f }, UIStackHint_Fixed(30.0f)));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, TestUI_GetPixelBox(run, &Fixture, Big).height == 600.0f);
        TEST_CHECK(run, TestUI_GetPixelBox(run, &Fixture, Fill).height == 0.0f);
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, After), 0.0f, 600.0f, 1000.0f, 30.0f));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A stack's preferred size is its children's: fill counting with its content, gaps only between what shows. */
static void AStacksPreferredSizeIsItsChildrens(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterSizedType(run, &Fixture);
        uint64_t Row = AddStack(run, &Fixture, WIDGET_ID_INVALID, UIStackDirection_Row, UIInsets_All(0.0f), 0.0f);
        UIStackOptions Options = UIStackOptions_Create(UIStackDirection_Column);
        Options.Layout.Padding = (UIInsets){ .Left = 1.0f, .Top = 2.0f, .Right = 3.0f, .Bottom = 4.0f };
        Options.Layout.Spacing = 5.0f;
        uint64_t Column = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, Row, &Options, &Column));
        AddSized(run, &Fixture, Type, Column, (Vector2){ .x = 40.0f, .y = 10.0f }, UIStackHint_Content());
        AddSized(run, &Fixture, Type, Column, (Vector2){ .x = 60.0f, .y = 999.0f }, UIStackHint_Fixed(20.0f));
        AddSized(run, &Fixture, Type, Column, (Vector2){ .x = 20.0f, .y = 7.0f }, UIStackHint_Fill(9.0f));
        TestUI_Ticks(run, &Fixture, 3U);

        Widget* ColumnWidget = NULL;
        Vector2 Size = { 0 };
        RenderContext Measure;
        RenderContext_CreateMeasuring(&Measure, (Vector2){ .x = 1000.0f, .y = 500.0f }, 2.0f, RenderTargetPosition_Centered());
        UIMeasureContext Context = { .Measure = &Measure };
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Column, &ColumnWidget))
            && TEST_CHECK_SUCCESS(run, Widget_Measure(ColumnWidget, &Context, UI_MEASURE_UNBOUNDED_WIDTH, &Size)))
        {
            TEST_CHECK(run, (Size.x == 64.0f) && (Size.y == 53.0f)); // 60 + 1 + 3 wide; 10 + 20 + 7, two gaps, 2 + 4 tall
        }
        // In the row it is given that width.
        TEST_CHECK(run, TestUI_GetPixelBox(run, &Fixture, Column).width == 64.0f);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* What wraps gets the width it will have: a column's child its width, a row's filling child its share. */
static void WrappingChildrenAreMeasuredAtTheirWidth(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterSizedType(run, &Fixture);
        uint64_t Column = AddStack(run, &Fixture, WIDGET_ID_INVALID, UIStackDirection_Column, UIInsets_All(10.0f), 0.0f);
        SizedWidget* Wrapped = AddSized(run, &Fixture, Type, Column, (Vector2){ .x = 1.0f, .y = 1.0f }, UIStackHint_Content());
        if (Wrapped != NULL)
        {
            Wrapped->IsWrapping = true;
            Wrapped->Area = 9800.0f; // At the width 980, 10 tall.
            Widget_InvalidateMeasure(&Wrapped->Base);
            TestUI_Ticks(run, &Fixture, 2U);
            TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, IdOf(Wrapped)), 10.0f, 10.0f, 980.0f, 10.0f));
            TEST_CHECK(run, Wrapped->LastAvailableWidth == 980.0f);

            uint64_t Row = AddStack(run, &Fixture, Column, UIStackDirection_Row, UIInsets_All(0.0f), 0.0f);
            AddSized(run, &Fixture, Type, Row, (Vector2){ .x = 180.0f, .y = 1.0f }, UIStackHint_Fixed(180.0f));
            UIStackHint Fill = UIStackHint_Fill(1.0f);
            Fill.Align = UIStackAlign_Start;
            SizedWidget* Filling = AddSized(run, &Fixture, Type, Row, (Vector2){ .x = 1.0f, .y = 1.0f }, Fill);
            Filling->IsWrapping = true;
            Filling->Area = 8000.0f;
            TestUI_Ticks(run, &Fixture, 3U);
            TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, IdOf(Filling)), 190.0f, 20.0f, 800.0f, 10.0f));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A fixed child is measured at its size and a content child at what is left: the same widths every tick, so the cache answers. */
static void ARowMeasuresEachChildAtTheWidthItGets(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterSizedType(run, &Fixture);
        uint64_t Row = AddStack(run, &Fixture, WIDGET_ID_INVALID, UIStackDirection_Row, UIInsets_All(0.0f), 0.0f);
        UIStackHint Fixed = UIStackHint_Fixed(200.0f);
        Fixed.Align = UIStackAlign_Start;
        SizedWidget* First = AddSized(run, &Fixture, Type, Row, (Vector2){ .x = 1.0f, .y = 1.0f }, Fixed);
        UIStackHint Content = UIStackHint_Content();
        Content.Align = UIStackAlign_Start;
        SizedWidget* Second = AddSized(run, &Fixture, Type, Row, (Vector2){ .x = 1.0f, .y = 1.0f }, Content);
        if ((First != NULL) && (Second != NULL))
        {
            First->IsWrapping = true;
            First->Area = 8000.0f;
            Second->IsWrapping = true;
            Second->Area = 8000.0f;
            Widget_InvalidateMeasure(&First->Base);
            Widget_InvalidateMeasure(&Second->Base);
            TestUI_Ticks(run, &Fixture, 2U);
            TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, IdOf(First)), 0.0f, 0.0f, 200.0f, 40.0f));
            TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, IdOf(Second)), 200.0f, 0.0f, 800.0f, 10.0f));

            int FirstCount = First->MeasureCount;
            int SecondCount = Second->MeasureCount;
            TestUI_Ticks(run, &Fixture, 10U);
            TEST_CHECK(run, (First->MeasureCount == FirstCount) && (Second->MeasureCount == SecondCount));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A filling child's height comes from its own share, not from everything that is left. */
static void FillChildrenAreMeasuredAtTheirShares(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterSizedType(run, &Fixture);
        uint64_t Row = AddStack(run, &Fixture, WIDGET_ID_INVALID, UIStackDirection_Row, UIInsets_All(0.0f), 0.0f);
        UIStackHint One = UIStackHint_Fill(1.0f);
        One.Align = UIStackAlign_Start;
        UIStackHint Three = UIStackHint_Fill(3.0f);
        Three.Align = UIStackAlign_Start;
        SizedWidget* First = AddSized(run, &Fixture, Type, Row, (Vector2){ .x = 1.0f, .y = 1.0f }, One);
        SizedWidget* Second = AddSized(run, &Fixture, Type, Row, (Vector2){ .x = 1.0f, .y = 1.0f }, Three);
        if ((First != NULL) && (Second != NULL))
        {
            First->IsWrapping = true;
            First->Area = 7500.0f;
            Second->IsWrapping = true;
            Second->Area = 7500.0f;
            Widget_InvalidateMeasure(&First->Base);
            Widget_InvalidateMeasure(&Second->Base);
            TestUI_Ticks(run, &Fixture, 2U);
            TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, IdOf(First)), 0.0f, 0.0f, 250.0f, 30.0f));
            TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, IdOf(Second)), 250.0f, 0.0f, 750.0f, 10.0f));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* In a column a child's width is its cross size, unless it stretches: then the column's width is what it gets. */
static void AColumnMeasuresAChildAtItsCrossSize(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterSizedType(run, &Fixture);
        uint64_t Column = AddStack(run, &Fixture, WIDGET_ID_INVALID, UIStackDirection_Column, UIInsets_All(0.0f), 0.0f);
        UIStackHint Narrow = UIStackHint_Content();
        Narrow.Align = UIStackAlign_Start;
        Narrow.HasCrossSize = true;
        Narrow.CrossSize = 200.0f;
        UIStackHint Stretched = Narrow;
        Stretched.Align = UIStackAlign_Stretch;
        SizedWidget* First = AddSized(run, &Fixture, Type, Column, (Vector2){ .x = 1.0f, .y = 1.0f }, Narrow);
        SizedWidget* Second = AddSized(run, &Fixture, Type, Column, (Vector2){ .x = 1.0f, .y = 1.0f }, Stretched);
        if ((First != NULL) && (Second != NULL))
        {
            First->IsWrapping = true;
            First->Area = 8000.0f;
            Second->IsWrapping = true;
            Second->Area = 8000.0f;
            Widget_InvalidateMeasure(&First->Base);
            Widget_InvalidateMeasure(&Second->Base);
            TestUI_Ticks(run, &Fixture, 2U);
            TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, IdOf(First)), 0.0f, 0.0f, 200.0f, 40.0f));
            TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, IdOf(Second)), 0.0f, 40.0f, 1000.0f, 8.0f));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A stretched child's cross size is not used: not for its box, not for what the stack above answers. */
static void AStretchedChildsCrossSizeIsNotUsed(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterSizedType(run, &Fixture);
        uint64_t Outer = AddStack(run, &Fixture, WIDGET_ID_INVALID, UIStackDirection_Row, UIInsets_All(0.0f), 0.0f);
        uint64_t Inner = AddStack(run, &Fixture, Outer, UIStackDirection_Column, UIInsets_All(0.0f), 0.0f);
        UIStackHint Hint = UIStackHint_Content();
        Hint.HasCrossSize = true;
        Hint.CrossSize = 500.0f;
        SizedWidget* Sized = AddSized(run, &Fixture, Type, Inner, (Vector2){ .x = 10.0f, .y = 20.0f }, Hint);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Inner), 0.0f, 0.0f, 10.0f, 500.0f));
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, IdOf(Sized)), 0.0f, 0.0f, 10.0f, 20.0f));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Stacks nest to the tree's depth, and each level answers: the leaf's size plus every level's padding. */
static void StacksNestToTheTreesDepth(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterSizedType(run, &Fixture);
        uint64_t Parent = AddStack(run, &Fixture, WIDGET_ID_INVALID, UIStackDirection_Column, UIInsets_All(1.0f), 0.0f);
        size_t Levels = 1U;
        for (; (Levels + 1U) < WIDGET_MAX_TREE_DEPTH; Levels++)
        {
            Parent = AddStack(run, &Fixture, Parent, ((Levels % 2U) == 0U) ? UIStackDirection_Column : UIStackDirection_Row, UIInsets_All(1.0f), 0.0f);
        }
        SizedWidget* Leaf = AddSized(run, &Fixture, Type, Parent, (Vector2){ .x = 10.0f, .y = 10.0f }, UIStackHint_Content());
        TestUI_Ticks(run, &Fixture, 3U);
        // Column inside row inside column ...: the leaf's box lies inside every padding.
        Rectangle Box = TestUI_GetPixelBox(run, &Fixture, IdOf(Leaf));
        TEST_CHECK(run, TestUI_IsSamePixel(Box.x, (float)Levels) && TestUI_IsSamePixel(Box.y, (float)Levels));

        // One level more is the tree's own limit.
        UIStackOptions Options = UIStackOptions_Create(UIStackDirection_Column);
        uint64_t Refused = WIDGET_ID_INVALID;
        TEST_CHECK_ERROR(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, IdOf(Leaf), &Options, &Refused), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, Refused == WIDGET_ID_INVALID);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void AStackLetsThePointerThroughButNotItsChildren(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterSizedType(run, &Fixture);
        uint64_t Stack = AddStack(run, &Fixture, WIDGET_ID_INVALID, UIStackDirection_Column, UIInsets_All(0.0f), 0.0f);
        SizedWidget* Child = AddSized(run, &Fixture, Type, Stack, (Vector2){ .x = 100.0f, .y = 100.0f }, UIStackHint_Fixed(100.0f));
        TestUI_Ticks(run, &Fixture, 2U);
        Widget* Top = NULL;
        TEST_CHECK(run, UIScreen_TryGetTopmostAt(&Fixture.Screen, (Vector2){ .x = 0.05f, .y = 0.05f }, &Top) && (Top == &Child->Base));
        TEST_CHECK(run, !UIScreen_TryGetTopmostAt(&Fixture.Screen, (Vector2){ .x = 0.5f, .y = 0.9f }, &Top));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void BadStackOptionsAndHintsAreRefused(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        size_t Live = UIWidgetFactory_GetTotalLiveWidgetCount(&Fixture.Widgets);
        UIStackOptions Options = UIStackOptions_Create(UIStackDirection_Column);
        uint64_t Id = WIDGET_ID_INVALID;
        Options.Layout.Spacing = NAN;
        TEST_CHECK_ERROR(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Id), ErrorCode_ArgumentOutOfRange);
        Options.Layout.Spacing = -1.0f;
        TEST_CHECK_ERROR(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Id), ErrorCode_ArgumentOutOfRange);
        Options = UIStackOptions_Create(UIStackDirection_Column);
        Options.Layout.Padding.Right = INFINITY;
        TEST_CHECK_ERROR(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Id), ErrorCode_ArgumentOutOfRange);
        Options = UIStackOptions_Create((UIStackDirection)7);
        TEST_CHECK_ERROR(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Id), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, Id == WIDGET_ID_INVALID);
        TEST_CHECK(run, UIWidgetFactory_GetTotalLiveWidgetCount(&Fixture.Widgets) == Live);

        UIStackHint Hint = UIStackHint_Fixed(10.0f);
        TEST_CHECK_ERROR(run, StackWidget_SetChildHint(&Fixture.Screen, 12345U, &Hint), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, StackWidget_SetChildHint(NULL, 1U, &Hint), ErrorCode_IllegalArgument);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void AChildsHintIsSetThroughTheStack(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterSizedType(run, &Fixture);
        uint64_t Stack = AddStack(run, &Fixture, WIDGET_ID_INVALID, UIStackDirection_Column, UIInsets_All(0.0f), 0.0f);
        uint64_t Child = IdOf(AddSized(run, &Fixture, Type, Stack, (Vector2){ .x = 10.0f, .y = 20.0f }, UIStackHint_Content()));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, TestUI_GetPixelBox(run, &Fixture, Child).height == 20.0f);
        UIStackHint Hint = UIStackHint_Fixed(44.0f);
        TEST_CHECK_SUCCESS(run, StackWidget_SetChildHint(&Fixture.Screen, Child, &Hint));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, TestUI_GetPixelBox(run, &Fixture, Child).height == 44.0f);
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Tests: labels in UI pixels.
static void AKitLabelsLineHeightIsInUIPixels(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture) && TestUI_SetTheme(run, &Fixture, PLAIN_THEME))
    {
        uint64_t Stack = AddStack(run, &Fixture, WIDGET_ID_INVALID, UIStackDirection_Column, UIInsets_All(0.0f), 0.0f);
        UILabelOptions Options = UILabelOptions_Create(u8"abcd");
        Options.Element.Hint.Align = UIStackAlign_Start;
        uint64_t Label = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateLabel(&Fixture.Kit, &Fixture.Screen, Stack, &Options, &Label));
        TestUI_Ticks(run, &Fixture, 2U);

        // The theme's 14 pixels tall: the monospace glyphs advance half that, so 4 of them are 28 wide.
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Label), 0.0f, 0.0f, 28.0f, 14.0f));

        // Another surface does not change it.
        TestUI_SetSurface(run, &Fixture, 1920.0f, 1080.0f);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Label), 0.0f, 0.0f, 28.0f, 14.0f));

        // The scale does.
        TEST_CHECK_SUCCESS(run, UIScreen_SetScale(&Fixture.Screen, 2.0f));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Label), 0.0f, 0.0f, 56.0f, 28.0f));

        // Its own size replaces the theme's, and goes back to it.
        TEST_CHECK_SUCCESS(run, UILabel_SetTextSize(&Fixture.Screen, Label, true, 16.0f));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Label), 0.0f, 0.0f, 64.0f, 32.0f));
        TEST_CHECK_SUCCESS(run, UILabel_SetTextSize(&Fixture.Screen, Label, false, 0.0f));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Label), 0.0f, 0.0f, 56.0f, 28.0f));

        // New text is a new preferred size.
        TEST_CHECK_SUCCESS(run, UILabel_SetText(&Fixture.Screen, Label, u8"abcdefgh"));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Label), 0.0f, 0.0f, 112.0f, 28.0f));
        TEST_CHECK_ERROR(run, UILabel_SetText(&Fixture.Screen, 777U, u8"x"), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UILabel_SetText(&Fixture.Screen, Label, NULL), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, UILabel_SetTextSize(&Fixture.Screen, Label, true, -1.0f), ErrorCode_ArgumentOutOfRange);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A label asked for a size in pixels keeps its Size to it: a plain label, its text in fitted units until asked. */
static void APlainLabelCanAskForUIPixels(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t LabelType = WIDGET_TYPE_ID_INVALID;
        TEST_CHECK_SUCCESS(run, LabelWidget_RegisterType(&Fixture.Widgets, &LabelType));
        StringComponent* String = NULL;
        TEST_CHECK_SUCCESS(run, TextComponentFactory_CreateString(&Fixture.Components, u8"abcd", &String));
        TEST_CHECK_SUCCESS(run, StringComponent_SetSize(String, 1.0f));
        LabelWidget* Label = NULL;
        if (TEST_CHECK_SUCCESS(run, LabelWidget_Create(&Fixture.Screen, LabelType, &Fixture.Components, Fixture.Assets.Manager, NULL,
            StringComponent_AsComponent(String), &Label)))
        {
            TEST_CHECK_SUCCESS(run, TextDocument_SetDefaultFontName(LabelWidget_GetDocument(Label), u8"mono"));
            TEST_CHECK_SUCCESS(run, LabelWidget_SetSize(Label, 0.1f));
            TEST_CHECK_SUCCESS(run, Widget_InitializeWidget(LabelWidget_AsWidget(Label)));
            TEST_CHECK_SUCCESS(run, UIScreen_AddWidget(&Fixture.Screen, LabelWidget_AsWidget(Label)));
            uint64_t Id = Widget_GetId(LabelWidget_AsWidget(Label));
            TestUI_Ticks(run, &Fixture, 2U);
            TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Id), 0.0f, 0.0f, 100.0f, 50.0f)); // 0.1 of 500 tall

            TEST_CHECK_SUCCESS(run, LabelWidget_SetUIPixelSize(Label, 20.0f));
            TestUI_Ticks(run, &Fixture, 2U);
            TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Id), 0.0f, 0.0f, 40.0f, 20.0f));
            TEST_CHECK_ERROR(run, LabelWidget_SetUIPixelSize(Label, 0.0f), ErrorCode_ArgumentOutOfRange);
            TEST_CHECK_ERROR(run, LabelWidget_SetUIPixelSize(NULL, 1.0f), ErrorCode_IllegalArgument);

            // Given back: the size stays what it became, and is the label's own.
            TEST_CHECK_SUCCESS(run, LabelWidget_ClearUIPixelSize(Label));
            TestUI_SetSurface(run, &Fixture, 2000.0f, 1000.0f);
            TestUI_Ticks(run, &Fixture, 2U);
            TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Id), 0.0f, 0.0f, 80.0f, 40.0f));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A placed label keeps the box it is given; a wrapped one lines its text up to that width. */
static void APlacedLabelKeepsItsBoxAndWrapsToIt(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture) && TestUI_SetTheme(run, &Fixture, PLAIN_THEME))
    {
        UILabelOptions Options = UILabelOptions_Create(u8"aaaa bbbb cccc");
        Options.Element.HasPlacement = true;
        Options.Element.Placement = UIPlacement_FromTopLeft(10.0f, 10.0f, 200.0f, 50.0f);
        uint64_t Placed = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateLabel(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Placed));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Placed), 10.0f, 10.0f, 200.0f, 50.0f));

        // In a column 50 pixels wide, the words wrap: three lines of 14.
        uint64_t Column = WIDGET_ID_INVALID;
        UIStackOptions Stack = UIStackOptions_Create(UIStackDirection_Column);
        Stack.Element.HasPlacement = true;
        Stack.Element.Placement = UIPlacement_FromTopLeft(0.0f, 100.0f, 50.0f, 300.0f);
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Stack, &Column));
        UILabelOptions Wrapped = UILabelOptions_Create(u8"aaaa bbbb cccc");
        Wrapped.IsWrapped = true;
        Wrapped.Element.Hint.Align = UIStackAlign_Stretch;
        uint64_t WrappedId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateLabel(&Fixture.Kit, &Fixture.Screen, Column, &Wrapped, &WrappedId));
        TestUI_Ticks(run, &Fixture, 3U);
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, WrappedId), 0.0f, 100.0f, 50.0f, 42.0f));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A wrapped label answers for the width it is asked, in UI pixels at any scale, and unbounded it is one line again. */
static void AWrappedLabelAnswersForTheWidthItIsAsked(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture) && TestUI_SetTheme(run, &Fixture, PLAIN_THEME))
    {
        UILabelOptions Options = UILabelOptions_Create(u8"aaaa bbbb cccc");
        Options.IsWrapped = true;
        uint64_t Id = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateLabel(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Id));
        TestUI_Tick(run, &Fixture);
        Widget* Base = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base)))
        {
            RenderContext Measure;
            RenderContext_CreateMeasuring(&Measure, (Vector2){ .x = 1000.0f, .y = 500.0f }, 2.0f, RenderTargetPosition_Centered());
            UIMeasureContext Context = { .Measure = &Measure };
            Vector2 Size = { .x = 0.0f, .y = 0.0f };
            TEST_CHECK_SUCCESS(run, Widget_Measure(Base, &Context, 50.0f, &Size));
            TEST_CHECK(run, TestUI_IsSamePixel(Size.y, 42.0f)); // Three lines of 14.
            TEST_CHECK_SUCCESS(run, Widget_Measure(Base, &Context, UI_MEASURE_UNBOUNDED_WIDTH, &Size));
            TEST_CHECK(run, TestUI_IsSamePixel(Size.y, 14.0f) && TestUI_IsSamePixel(Size.x, 98.0f)); // 14 glyphs of 7, on one line.
            TEST_CHECK_SUCCESS(run, Widget_Measure(Base, &Context, 50.0f, &Size));
            TEST_CHECK(run, TestUI_IsSamePixel(Size.y, 42.0f));

            // At twice the scale a width of 70 UI pixels is 140 surface pixels, and glyphs are 14 wide: two words fit.
            TEST_CHECK_SUCCESS(run, UIScreen_SetScale(&Fixture.Screen, 2.0f));
            TEST_CHECK_SUCCESS(run, Widget_Measure(Base, &Context, 70.0f, &Size));
            TEST_CHECK(run, TestUI_IsSamePixel(Size.y, 28.0f));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* What changes a label through its own calls drops the cached answer at once; what changes behind its back, at its next Update. */
static void ALabelsChangesDropTheCachedAnswer(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture) && TestUI_SetTheme(run, &Fixture, PLAIN_THEME))
    {
        uint64_t Stack = AddStack(run, &Fixture, WIDGET_ID_INVALID, UIStackDirection_Column, UIInsets_All(0.0f), 0.0f);
        UILabelOptions Options = UILabelOptions_Create(u8"abcd");
        Options.Element.Hint.Align = UIStackAlign_Start;
        uint64_t Id = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateLabel(&Fixture.Kit, &Fixture.Screen, Stack, &Options, &Id));
        TestUI_Ticks(run, &Fixture, 2U);
        Widget* Base = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base)))
        {
            LabelWidget* Label = (LabelWidget*)Base;
            TEST_CHECK(run, Base->_isMeasureValid);
            TEST_CHECK_SUCCESS(run, UILabel_SetText(&Fixture.Screen, Id, u8"abcde"));
            TEST_CHECK(run, !Base->_isMeasureValid);
            TestUI_Ticks(run, &Fixture, 2U);
            TEST_CHECK(run, Base->_isMeasureValid);
            TEST_CHECK_SUCCESS(run, UILabel_SetTextSize(&Fixture.Screen, Id, true, 20.0f));
            TEST_CHECK(run, !Base->_isMeasureValid);
            TestUI_Ticks(run, &Fixture, 2U);
            TEST_CHECK_SUCCESS(run, LabelWidget_SetUIPixelSize(Label, 18.0f));
            TEST_CHECK(run, !Base->_isMeasureValid);
            TestUI_Ticks(run, &Fixture, 2U);
            TEST_CHECK_SUCCESS(run, LabelWidget_ClearUIPixelSize(Label));
            TEST_CHECK(run, !Base->_isMeasureValid);
            TestUI_Ticks(run, &Fixture, 2U);
            TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Id), 0.0f, 0.0f, 35.0f, 14.0f));

            // Text changed in the document itself: the label finds out in its Update, and the stack follows.
            StringComponent* String = TextComponent_AsStringComponent(TextDocument_GetRoot(LabelWidget_GetDocument(Label)));
            TEST_CHECK_SUCCESS(run, StringComponent_SetText(String, u8"abcdefgh"));
            TestUI_Ticks(run, &Fixture, 2U);
            TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Id), 0.0f, 0.0f, 56.0f, 14.0f));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Panels sharing a row, a wrapped label in each: once laid out, a tick asks nobody anything. */
static void WrappedLabelsInSharedPanelsSettle(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture) && TestUI_SetTheme(run, &Fixture, PLAIN_THEME))
    {
        WidgetTypeDescriptor Descriptor = { .Name = u8"CountingStack", .StructSize = sizeof(CountingStackWidget), .VTable = &CountingStackVTable };
        uint64_t Type = WIDGET_TYPE_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIWidgetFactory_RegisterType(&Fixture.Widgets, &Descriptor, &Type));
        uint64_t Page = AddStack(run, &Fixture, WIDGET_ID_INVALID, UIStackDirection_Column, UIInsets_All(0.0f), 0.0f);
        UIStackOptions RowOptions = UIStackOptions_Create(UIStackDirection_Row);
        RowOptions.Element.Hint = UIStackHint_Fill(1.0f);
        uint64_t Row = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, Page, &RowOptions, &Row));
        CountingStackWidget* First = AddCountingColumn(run, &Fixture, Type, Row, UIStackHint_Fill(1.0f));
        CountingStackWidget* Second = AddCountingColumn(run, &Fixture, Type, Row, UIStackHint_Fill(2.0f));
        if ((First != NULL) && (Second != NULL))
        {
            uint64_t FirstLabel = AddWrappedLabel(run, &Fixture, Widget_GetId(&First->Base), u8"some words that wrap in a narrow panel like this one of a third of the row");
            uint64_t SecondLabel = AddWrappedLabel(run, &Fixture, Widget_GetId(&Second->Base), u8"other words that wrap in a panel twice as wide");
            TestUI_Ticks(run, &Fixture, 5U);
            int FirstCount = First->MeasureCount;
            int SecondCount = Second->MeasureCount;
            Widget* Label = NULL;
            TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, FirstLabel, &Label));
            uint64_t TextRevision = (Label != NULL) ? ((LabelWidget*)Label)->_layout._documentRevision : 0U;
            Vector2 TextSize = (Label != NULL) ? TextLayout_GetSize(&((LabelWidget*)Label)->_layout) : (Vector2){ .x = 0.0f, .y = 0.0f };
            Rectangle Box = TestUI_GetPixelBox(run, &Fixture, FirstLabel);
            TestUI_Ticks(run, &Fixture, 100U);
            TEST_CHECK(run, (First->MeasureCount == FirstCount) && (Second->MeasureCount == SecondCount));
            Rectangle After = TestUI_GetPixelBox(run, &Fixture, FirstLabel);
            TEST_CHECK(run, TestUI_IsBox(After, Box.x, Box.y, Box.width, Box.height));
            if (Label != NULL)
            {
                Vector2 Later = TextLayout_GetSize(&((LabelWidget*)Label)->_layout);
                TEST_CHECK(run, (Later.x == TextSize.x) && (Later.y == TextSize.y));
                TEST_CHECK(run, ((LabelWidget*)Label)->_layout._documentRevision == TextRevision);
            }
            TEST_CHECK(run, TestUI_GetPixelBox(run, &Fixture, SecondLabel).width > 0.0f);
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A row as tall as what it holds, where what it holds wraps at each one's share of the row. */
static void AContentRowIsAsTallAsItsWrappedText(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture) && TestUI_SetTheme(run, &Fixture, PLAIN_THEME))
    {
        uint64_t Page = AddStack(run, &Fixture, WIDGET_ID_INVALID, UIStackDirection_Column, UIInsets_All(0.0f), 0.0f);
        UIStackOptions RowOptions = UIStackOptions_Create(UIStackDirection_Row);
        uint64_t Row = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, Page, &RowOptions, &Row));
        UIStackOptions PanelOptions = UIStackOptions_Create(UIStackDirection_Column);
        PanelOptions.Element.Hint = UIStackHint_Fill(1.0f);
        uint64_t First = WIDGET_ID_INVALID;
        uint64_t Second = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, Row, &PanelOptions, &First));
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, Row, &PanelOptions, &Second));
        // At 7 pixels a glyph, 14 words of 5 glyphs fit each 500 pixel panel: 30 of them are 3 lines, 15 of them 2.
        uint64_t Three = AddWrappedLabel(run, &Fixture, First, u8"aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa "
            "aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa");
        uint64_t Two = AddWrappedLabel(run, &Fixture, Second, u8"bbbb bbbb bbbb bbbb bbbb bbbb bbbb bbbb bbbb bbbb bbbb bbbb bbbb bbbb bbbb");
        TestUI_Ticks(run, &Fixture, 3U);
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Row), 0.0f, 0.0f, 1000.0f, 42.0f));
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Three), 0.0f, 0.0f, 500.0f, 42.0f));
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Two), 500.0f, 0.0f, 500.0f, 28.0f));

        // What it draws is wrapped as it was measured.
        Widget* ThreeBase = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Three, &ThreeBase)))
        {
            TEST_CHECK(run, fabsf((TextLayout_GetSize(&((LabelWidget*)ThreeBase)->_layout).y * 500.0f) - 42.0f) < 0.01f);
        }

        // A gap between the panels comes off what they share: at 450 pixels each, 14 words of 5 glyphs are 2 lines, where 500 would hold them.
        UIStackOptions GappedOptions = UIStackOptions_Create(UIStackDirection_Row);
        GappedOptions.Layout.Spacing = 100.0f;
        uint64_t Gapped = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, Page, &GappedOptions, &Gapped));
        uint64_t Left = WIDGET_ID_INVALID;
        uint64_t Right = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, Gapped, &PanelOptions, &Left));
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, Gapped, &PanelOptions, &Right));
        AddWrappedLabel(run, &Fixture, Left, u8"cccc cccc cccc cccc cccc cccc cccc cccc cccc cccc cccc cccc cccc cccc");
        TestUI_Ticks(run, &Fixture, 3U);
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Gapped), 0.0f, 42.0f, 1000.0f, 28.0f));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A wrapped label that is not stretched gets a box for the text it was measured with, rounded up: its text stays on the lines it was measured on. */
static void AWrappedLabelsBoxHoldsItsText(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture) && TestUI_SetTheme(run, &Fixture, PLAIN_THEME))
    {
        TEST_CHECK_SUCCESS(run, UIScreen_SetScale(&Fixture.Screen, 1.25f));
        UIStackOptions Stack = UIStackOptions_Create(UIStackDirection_Column);
        Stack.Element.HasPlacement = true;
        Stack.Element.Placement = UIPlacement_FromTopLeft(0.0f, 0.0f, 90.0f, 300.0f);
        uint64_t Column = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Stack, &Column));
        // At 13 UI pixels and a scale of 1.25 a glyph is 8.125 surface pixels: 9 of them are 73.125, which rounded down would wrap.
        UILabelOptions Options = UILabelOptions_Create(u8"aaaa bbbb");
        Options.IsWrapped = true;
        Options.HasTextSize = true;
        Options.TextSize = 13.0f;
        uint64_t Id = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateLabel(&Fixture.Kit, &Fixture.Screen, Column, &Options, &Id));
        TestUI_Ticks(run, &Fixture, 4U);
        Widget* Base = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base)))
        {
            Rectangle Box = TestUI_GetPixelBox(run, &Fixture, Id);
            Vector2 Text = TextLayout_GetSize(&((LabelWidget*)Base)->_layout);
            TEST_CHECK(run, fabsf((Text.y * 500.0f) - 16.25f) < 0.01f); // Still the one line.
            TEST_CHECK(run, fabsf((Text.x * 500.0f) - 73.125f) < 0.01f);
            TEST_CHECK(run, (Box.width > 72.9f) && (Box.width < 74.1f));
            TEST_CHECK(run, fabsf(Box.width - floorf(Box.width + 0.5f)) < 0.01f); // Whole pixels.

            // 3 glyphs are 24.375: the box is 25, where the nearest pixel would leave the text sticking out.
            Options = UILabelOptions_Create(u8"aaa");
            Options.HasTextSize = true;
            Options.TextSize = 13.0f;
            Options.Element.Hint.Align = UIStackAlign_Start;
            uint64_t Short = WIDGET_ID_INVALID;
            TEST_CHECK_SUCCESS(run, UIKit_CreateLabel(&Fixture.Kit, &Fixture.Screen, Column, &Options, &Short));
            TestUI_Ticks(run, &Fixture, 3U);
            TEST_CHECK(run, TestUI_IsSamePixel(TestUI_GetPixelBox(run, &Fixture, Short).width, 25.0f));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Navigation inside a container that gets hidden ends there, and starts anew without it. */
static void AHiddenContainerEndsTheNavigationInside(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        WidgetTypeDescriptor Descriptor = { .Name = u8"FocusableSized", .StructSize = sizeof(SizedWidget), .VTable = &SizedVTable, .IsFocusable = true };
        uint64_t Type = WIDGET_TYPE_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIWidgetFactory_RegisterType(&Fixture.Widgets, &Descriptor, &Type));
        SizedWidget* Container = AddSized(run, &Fixture, Type, WIDGET_ID_INVALID, (Vector2){ .x = 1.0f, .y = 1.0f }, UIStackHint_Content());
        SizedWidget* Inside = AddSized(run, &Fixture, Type, IdOf(Container), (Vector2){ .x = 1.0f, .y = 1.0f }, UIStackHint_Content());
        SizedWidget* Other = AddSized(run, &Fixture, Type, WIDGET_ID_INVALID, (Vector2){ .x = 1.0f, .y = 1.0f }, UIStackHint_Content());
        if ((Container != NULL) && (Inside != NULL) && (Other != NULL))
        {
            UIPlacement ContainerBox = UIPlacement_FromTopLeft(10.0f, 10.0f, 200.0f, 100.0f);
            UIPlacement InsideBox = UIPlacement_FromTopLeft(10.0f, 10.0f, 50.0f, 50.0f);
            UIPlacement OtherBox = UIPlacement_FromTopLeft(300.0f, 10.0f, 50.0f, 50.0f);
            TEST_CHECK_SUCCESS(run, Widget_SetPlacement(&Container->Base, &ContainerBox));
            TEST_CHECK_SUCCESS(run, Widget_SetPlacement(&Inside->Base, &InsideBox));
            TEST_CHECK_SUCCESS(run, Widget_SetPlacement(&Other->Base, &OtherBox));
            TestUI_Tick(run, &Fixture);

            TestUI_Tap(run, &Fixture, GameKey_Tab);
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == IdOf(Container));
            TestUI_Tap(run, &Fixture, GameKey_Enter);
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == IdOf(Inside));
            TEST_CHECK(run, UIScreen_IsNavigating(&Fixture.Screen));

            TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(&Container->Base, false));
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID);
            TestUI_Tap(run, &Fixture, GameKey_Tab);
            TEST_CHECK(run, !UIScreen_IsNavigating(&Fixture.Screen));
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID);

            TestUI_Tap(run, &Fixture, GameKey_Tab);
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == IdOf(Other));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Public functions.
void UILayoutTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "placement_resolves_anchors_and_offsets", PlacementResolvesAnchorsAndOffsets },
        { "placement_scales_offsets_and_rounds_to_whole_pixels", PlacementScalesOffsetsAndRoundsToWholePixels },
        { "neighbours_share_their_edge_at_any_scale", NeighboursShareTheirEdgeAtAnyScale },
        { "placements_of_no_size_give_zero_never_nan", PlacementsOfNoSizeGiveZeroNeverNaN },
        { "a_box_turned_inside_out_has_no_size", ABoxTurnedInsideOutHasNoSize },
        { "a_box_of_whole_pixels_keeps_its_length_wherever_it_lies", ABoxOfWholePixelsKeepsItsLengthWhereverItLies },
        { "lengths_too_long_to_add_up_are_refused", LengthsTooLongToAddUpAreRefused },
        { "the_longest_lengths_still_add_up", TheLongestLengthsStillAddUp },
        { "placements_and_hints_that_are_not_valid_are_refused", PlacementsAndHintsThatAreNotValidAreRefused },
        { "a_placed_widget_follows_the_surface_and_the_scale", APlacedWidgetFollowsTheSurfaceAndTheScale },
        { "nested_placements_land_on_whole_pixels_at_any_depth", NestedPlacementsLandOnWholePixelsAtAnyDepth },
        { "widgets_without_a_placement_do_not_change", WidgetsWithoutAPlacementDoNotChange },
        { "clearing_a_placement_keeps_the_box", ClearingAPlacementKeepsTheBox },
        { "a_placement_wins_over_an_animation", APlacementWinsOverAnAnimation },
        { "an_idle_placement_does_not_hit_test_again", AnIdlePlacementDoesNotHitTestAgain },
        { "measuring_is_cached_until_something_changes", MeasuringIsCachedUntilSomethingChanges },
        { "a_change_below_drops_the_cache_all_the_way_up", AChangeBelowDropsTheCacheAllTheWayUp },
        { "the_generation_counts_a_scale_and_a_surface_that_change", TheGenerationCountsAScaleAndASurfaceThatChange },
        { "a_childs_new_hint_drops_the_cache_above", AChildsNewHintDropsTheCacheAbove },
        { "measure_answers_stay_finite_and_widths_compare_without_noise", MeasureAnswersStayFiniteAndWidthsCompareWithoutNoise },
        { "a_column_sizes_each_mode_with_padding_and_spacing", AColumnSizesEachModeWithPaddingAndSpacing },
        { "a_row_sizes_each_mode_with_padding_and_spacing", ARowSizesEachModeWithPaddingAndSpacing },
        { "children_align_across_the_stack", ChildrenAlignAcrossTheStack },
        { "hidden_children_take_no_room", HiddenChildrenTakeNoRoom },
        { "a_hidden_fill_child_takes_no_share", AHiddenFillChildTakesNoShare },
        { "a_hidden_widget_is_not_there_for_the_pointer", AHiddenWidgetIsNotThereForThePointer },
        { "a_hidden_container_ends_the_navigation_inside", AHiddenContainerEndsTheNavigationInside },
        { "overflow_is_laid_out_as_it_is", OverflowIsLaidOutAsItIs },
        { "a_stacks_preferred_size_is_its_childrens", AStacksPreferredSizeIsItsChildrens },
        { "wrapping_children_are_measured_at_their_width", WrappingChildrenAreMeasuredAtTheirWidth },
        { "a_row_measures_each_child_at_the_width_it_gets", ARowMeasuresEachChildAtTheWidthItGets },
        { "fill_children_are_measured_at_their_shares", FillChildrenAreMeasuredAtTheirShares },
        { "a_column_measures_a_child_at_its_cross_size", AColumnMeasuresAChildAtItsCrossSize },
        { "a_stretched_childs_cross_size_is_not_used", AStretchedChildsCrossSizeIsNotUsed },
        { "stacks_nest_to_the_trees_depth", StacksNestToTheTreesDepth },
        { "a_stack_lets_the_pointer_through_but_not_its_children", AStackLetsThePointerThroughButNotItsChildren },
        { "bad_stack_options_and_hints_are_refused", BadStackOptionsAndHintsAreRefused },
        { "a_childs_hint_is_set_through_the_stack", AChildsHintIsSetThroughTheStack },
        { "a_kit_labels_line_height_is_in_ui_pixels", AKitLabelsLineHeightIsInUIPixels },
        { "a_plain_label_can_ask_for_ui_pixels", APlainLabelCanAskForUIPixels },
        { "a_placed_label_keeps_its_box_and_wraps_to_it", APlacedLabelKeepsItsBoxAndWrapsToIt },
        { "a_wrapped_label_answers_for_the_width_it_is_asked", AWrappedLabelAnswersForTheWidthItIsAsked },
        { "a_labels_changes_drop_the_cached_answer", ALabelsChangesDropTheCachedAnswer },
        { "wrapped_labels_in_shared_panels_settle", WrappedLabelsInSharedPanelsSettle },
        { "a_content_row_is_as_tall_as_its_wrapped_text", AContentRowIsAsTallAsItsWrappedText },
        { "a_wrapped_labels_box_holds_its_text", AWrappedLabelsBoxHoldsItsText },
    };
    TestRun_RunSuite(run, "UILayout", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
