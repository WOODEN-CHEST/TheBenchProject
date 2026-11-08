#include "TestSuites.h"
#include "TestUI.h"
#include "UIKit.h"
#include "UIScrollArea.h"
#include "UIScrollable.h"
#include "UIScrollbar.h"
#include "UIWidgetFactory.h"
#include <math.h>


// Macros.
/** The fixture's look: 10 pixel bars with a 10 pixel shortest thumb and no inset, flat boxes. */
#define SCROLL_THEME \
    "{\"name\": \"scrolling\", \"font\": \"mono\", \"text_size\": 16, \"elements\": {" \
    "\"panel\": {\"padding\": [0, 0], \"spacing\": 0, \"background\": {\"kind\": \"flat\"}}," \
    "\"label\": {\"text\": {\"normal\": \"#d0d0d0\"}}," \
    "\"button\": {\"padding\": [8, 4], \"min_size\": [24, 24], \"background\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#e0e0e0\"}}," \
    "\"slider\": {\"min_size\": [64, 24], \"thumb_size\": [20, 20], \"track\": {\"kind\": \"flat\"}, \"fill\": {\"kind\": \"flat\"}, \"thumb\": {\"kind\": \"flat\"}}," \
    "\"scrollbar\": {\"thickness\": 10, \"min_thumb_length\": 10, \"thumb_inset\": 0, \"track\": {\"kind\": \"flat\"}, \"thumb\": {\"kind\": \"flat\"}}," \
    "\"scroll_area\": {\"background\": {\"kind\": \"flat\"}}}}"
/** The area the tests scroll: at (100, 50), 200 x 100. */
#define AREA_X 100.0f
#define AREA_Y 50.0f
#define AREA_WIDTH 200.0f
#define AREA_HEIGHT 100.0f
#define BAR 10.0f


// Types.
/* Content of a set size that counts how often it is measured. */
typedef struct SizedBoxStruct
{
    Widget Base;
    Vector2 Size;
    size_t MeasureCount;
} SizedBox;

/* What the tests build on. */
typedef struct ScrollFixtureStruct
{
    TestUIFixture UI;
    uint64_t SizedBoxType;
} ScrollFixture;


// Static functions: the content.
static Error SizedBox_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)context;
    (void)availableWidth;
    SizedBox* Box = self;
    Box->MeasureCount++;
    *outSize = Box->Size;
    return Error_CreateSuccess();
}

static const WidgetVTable SizedBoxVTable = { .Measure = SizedBox_Measure };


// Static functions: the fixture.
static bool Construct(TestRun* run, ScrollFixture* fixture)
{
    fixture->SizedBoxType = WIDGET_TYPE_ID_INVALID;
    if (!TestUI_Construct(run, &fixture->UI) || !TestUI_SetTheme(run, &fixture->UI, SCROLL_THEME))
    {
        return false;
    }
    WidgetTypeDescriptor Descriptor = { .Name = u8"SizedBox", .StructSize = sizeof(SizedBox), .VTable = &SizedBoxVTable };
    return TEST_CHECK_SUCCESS(run, UIWidgetFactory_RegisterType(&fixture->UI.Widgets, &Descriptor, &fixture->SizedBoxType));
}

static void Deconstruct(TestRun* run, ScrollFixture* fixture)
{
    TestUI_Deconstruct(run, &fixture->UI);
}

static uint64_t AddArea(TestRun* run, ScrollFixture* fixture, uint64_t parentId, UIScrollMode horizontal, UIScrollMode vertical, UIPlacement placement)
{
    UIScrollAreaOptions Options = UIScrollAreaOptions_Create();
    Options.Horizontal = horizontal;
    Options.Vertical = vertical;
    Options.Element.HasPlacement = true;
    Options.Element.Placement = placement;
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIKit_CreateScrollArea(&fixture->UI.Kit, &fixture->UI.Screen, parentId, &Options, &Id));
    return Id;
}

static uint64_t AddMainArea(TestRun* run, ScrollFixture* fixture, UIScrollMode horizontal, UIScrollMode vertical)
{
    return AddArea(run, fixture, WIDGET_ID_INVALID, horizontal, vertical, UIPlacement_FromTopLeft(AREA_X, AREA_Y, AREA_WIDTH, AREA_HEIGHT));
}

/* Content of a set size under a parent. */
static uint64_t AddSizedBox(TestRun* run, ScrollFixture* fixture, uint64_t parentId, float width, float height)
{
    Widget* Made = NULL;
    Widget* Parent = NULL;
    if (!TEST_CHECK_SUCCESS(run, UIWidgetFactory_CreateWidget(&fixture->UI.Widgets, &fixture->UI.Screen, fixture->SizedBoxType, NULL, &Made)))
    {
        return WIDGET_ID_INVALID;
    }
    ((SizedBox*)Made)->Size = (Vector2){ .x = width, .y = height };
    uint64_t Id = Widget_GetId(Made);
    TEST_CHECK_SUCCESS(run, Widget_InitializeWidget(Made));
    if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&fixture->UI.Screen, parentId, &Parent)))
    {
        TEST_CHECK_SUCCESS(run, Widget_AddSubWidget(Parent, Made));
    }
    return Id;
}

static SizedBox* GetSizedBox(ScrollFixture* fixture, uint64_t id)
{
    Widget* Found = NULL;
    return UIScreen_TryGetWidgetById(&fixture->UI.Screen, id, &Found) ? (SizedBox*)(void*)Found : NULL;
}

static UIScrollAreaCapability* GetArea(ScrollFixture* fixture, uint64_t id)
{
    Widget* Found = NULL;
    return UIScreen_TryGetWidgetById(&fixture->UI.Screen, id, &Found) ? &((UIScrollAreaLookBase*)(void*)Found)->Area : NULL;
}

static Vector2 GetOffset(TestRun* run, ScrollFixture* fixture, uint64_t areaId)
{
    Vector2 Offset = { .x = NAN, .y = NAN };
    TEST_CHECK_SUCCESS(run, UIScrollable_GetOffset(&fixture->UI.Screen, areaId, &Offset));
    return Offset;
}

static bool IsNear(float value, float expected)
{
    return fabsf(value - expected) < 0.01f;
}

static uint64_t AddButton(TestRun* run, ScrollFixture* fixture, uint64_t parentId, const unsigned char* text, float height)
{
    UIButtonOptions Options = UIButtonOptions_Create(text);
    Options.Element.Hint = UIStackHint_Fixed(height);
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&fixture->UI.Kit, &fixture->UI.Screen, parentId, &Options, &Id));
    return Id;
}

static uint64_t AddColumn(TestRun* run, ScrollFixture* fixture, uint64_t parentId)
{
    UIStackOptions Options = UIStackOptions_Create(UIStackDirection_Column);
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&fixture->UI.Kit, &fixture->UI.Screen, parentId, &Options, &Id));
    return Id;
}


// Tests: the arithmetic.
static void TheOffsetShowingASpanMovesAsLittleAsItTakes(TestRun* run)
{
    // Inside: nothing moves. Before the view: its start. After: its end at the view's end.
    TEST_CHECK(run, UIScrollable_GetOffsetShowing(100.0f, 120.0f, 20.0f, 50.0f) == 100.0f);
    TEST_CHECK(run, UIScrollable_GetOffsetShowing(100.0f, 80.0f, 20.0f, 50.0f) == 80.0f);
    TEST_CHECK(run, UIScrollable_GetOffsetShowing(100.0f, 140.0f, 20.0f, 50.0f) == 110.0f);
    TEST_CHECK(run, UIScrollable_GetOffsetShowing(100.0f, 90.0f, 20.0f, 50.0f) == 90.0f);
    // Longer than the view: its start, unless the view is already inside it.
    TEST_CHECK(run, UIScrollable_GetOffsetShowing(0.0f, 200.0f, 100.0f, 50.0f) == 200.0f);
    TEST_CHECK(run, UIScrollable_GetOffsetShowing(220.0f, 200.0f, 100.0f, 50.0f) == 220.0f);
    TEST_CHECK(run, UIScrollable_GetOffsetShowing(260.0f, 200.0f, 100.0f, 50.0f) == 200.0f);
}

static void TheOffsetStaysInItsRange(TestRun* run)
{
    UIScrollableCapability Piece;
    UIScrollableCapability_Construct(&Piece, UIScrollMode_Auto, UIScrollMode_Auto, 0.0f);
    TEST_CHECK(run, Piece._wheelStep == UI_SCROLLABLE_DEFAULT_WHEEL_STEP);
    TEST_CHECK(run, !UIScrollableCapability_SetOffset(&Piece, (Vector2){ .x = 50.0f, .y = 50.0f })); // Nothing to scroll yet.
    UIScrollableCapability_SetSizes(&Piece, (Vector2){ .x = 300.0f, .y = 400.0f }, (Vector2){ .x = 100.0f, .y = 100.0f });
    Vector2 Range = UIScrollableCapability_GetRange(&Piece);
    TEST_CHECK(run, (Range.x == 200.0f) && (Range.y == 300.0f));
    TEST_CHECK(run, UIScrollableCapability_SetOffset(&Piece, (Vector2){ .x = 500.0f, .y = -20.0f }));
    TEST_CHECK(run, (Piece._offset.x == 200.0f) && (Piece._offset.y == 0.0f));
    // A component that is no number keeps the old one.
    TEST_CHECK(run, UIScrollableCapability_SetOffset(&Piece, (Vector2){ .x = NAN, .y = 120.0f }));
    TEST_CHECK(run, (Piece._offset.x == 200.0f) && (Piece._offset.y == 120.0f));

    // What it scrolled is told; at an end nothing moves.
    Vector2 Moved = UIScrollableCapability_ScrollBy(&Piece, (Vector2){ .x = 30.0f, .y = 30.0f });
    TEST_CHECK(run, (Moved.x == 0.0f) && (Moved.y == 30.0f));
    TEST_CHECK(run, UIScrollableCapability_CanScroll(&Piece, UIScrollAxis_Horizontal, -1.0f));
    TEST_CHECK(run, !UIScrollableCapability_CanScroll(&Piece, UIScrollAxis_Horizontal, 1.0f));
    TEST_CHECK(run, !UIScrollableCapability_CanScroll(&Piece, UIScrollAxis_Vertical, 0.0f));
    TEST_CHECK(run, !UIScrollableCapability_CanScroll(&Piece, UIScrollAxis_Count, 1.0f));

    // Content that shrinks and a view that grows clamp the offset.
    TEST_CHECK(run, UIScrollableCapability_SetSizes(&Piece, (Vector2){ .x = 300.0f, .y = 200.0f }, (Vector2){ .x = 100.0f, .y = 100.0f }));
    TEST_CHECK(run, Piece._offset.y == 100.0f);
    TEST_CHECK(run, UIScrollableCapability_SetSizes(&Piece, (Vector2){ .x = 300.0f, .y = 200.0f }, (Vector2){ .x = 250.0f, .y = 180.0f }));
    TEST_CHECK(run, (Piece._offset.x == 50.0f) && (Piece._offset.y == 20.0f));
    // Sizes that are no numbers count as nothing.
    UIScrollableCapability_SetSizes(&Piece, (Vector2){ .x = NAN, .y = -5.0f }, (Vector2){ .x = INFINITY, .y = 10.0f });
    TEST_CHECK(run, (Piece._contentSize.x == 0.0f) && (Piece._contentSize.y == 0.0f) && (Piece._viewSize.x == UI_LAYOUT_MAX_LENGTH));
    TEST_CHECK(run, (Piece._offset.x == 0.0f) && (Piece._offset.y == 0.0f));

    // An axis that does not scroll has no range, whatever the sizes.
    UIScrollableCapability Fixed;
    UIScrollableCapability_Construct(&Fixed, UIScrollMode_Disabled, UIScrollMode_Always, 30.0f);
    UIScrollableCapability_SetSizes(&Fixed, (Vector2){ .x = 900.0f, .y = 900.0f }, (Vector2){ .x = 100.0f, .y = 100.0f });
    TEST_CHECK(run, (UIScrollableCapability_GetRange(&Fixed).x == 0.0f) && (UIScrollableCapability_GetRange(&Fixed).y == 800.0f));
    TEST_CHECK(run, (Fixed._wheelStep == 30.0f) && UIScrollMode_IsValid(UIScrollMode_Always) && !UIScrollMode_IsValid((UIScrollMode)7));

    // Showing a box: as little as it takes, a bad box changes nothing.
    UIScrollableCapability_SetSizes(&Fixed, (Vector2){ .x = 100.0f, .y = 900.0f }, (Vector2){ .x = 100.0f, .y = 100.0f });
    TEST_CHECK(run, UIScrollableCapability_EnsureVisible(&Fixed, (Rectangle){ .x = 0.0f, .y = 450.0f, .width = 10.0f, .height = 20.0f }));
    TEST_CHECK(run, Fixed._offset.y == 370.0f);
    TEST_CHECK(run, !UIScrollableCapability_EnsureVisible(&Fixed, (Rectangle){ .x = 0.0f, .y = 400.0f, .width = 10.0f, .height = 20.0f }));
    TEST_CHECK(run, !UIScrollableCapability_EnsureVisible(&Fixed, (Rectangle){ .x = 0.0f, .y = 0.0f, .width = -1.0f, .height = 20.0f }));
    TEST_CHECK(run, !UIScrollableCapability_EnsureVisible(&Fixed, (Rectangle){ .x = 0.0f, .y = NAN, .width = 1.0f, .height = 20.0f }));
}

static void TheThumbsArithmetic(TestRun* run)
{
    float Start = -1.0f;
    float Length = -1.0f;
    // The view's share of the track, placed by the offset's share of the range.
    UIScrollbar_GetThumb(100.0f, 10.0f, 400.0f, 100.0f, 150.0f, &Start, &Length);
    TEST_CHECK(run, IsNear(Length, 25.0f) && IsNear(Start, 37.5f));
    // Never shorter than the minimum, which never passes the track.
    UIScrollbar_GetThumb(100.0f, 30.0f, 10000.0f, 100.0f, 9900.0f, &Start, &Length);
    TEST_CHECK(run, IsNear(Length, 30.0f) && IsNear(Start, 70.0f));
    UIScrollbar_GetThumb(20.0f, 30.0f, 10000.0f, 100.0f, 0.0f, &Start, &Length);
    TEST_CHECK(run, IsNear(Length, 20.0f) && IsNear(Start, 0.0f));
    // Nothing to scroll: the whole track. No track: nothing.
    UIScrollbar_GetThumb(100.0f, 10.0f, 80.0f, 100.0f, 0.0f, &Start, &Length);
    TEST_CHECK(run, (Length == 100.0f) && (Start == 0.0f));
    UIScrollbar_GetThumb(0.0f, 10.0f, 400.0f, 100.0f, 50.0f, &Start, &Length);
    TEST_CHECK(run, (Length == 0.0f) && (Start == 0.0f));
    UIScrollbar_GetThumb(NAN, 10.0f, 400.0f, 100.0f, 50.0f, &Start, &Length);
    TEST_CHECK(run, (Length == 0.0f) && (Start == 0.0f));
    // The inverse: back to the offset, clamped.
    TEST_CHECK(run, IsNear(UIScrollbar_GetOffsetAt(100.0f, 25.0f, 300.0f, 37.5f), 150.0f));
    TEST_CHECK(run, UIScrollbar_GetOffsetAt(100.0f, 25.0f, 300.0f, -40.0f) == 0.0f);
    TEST_CHECK(run, UIScrollbar_GetOffsetAt(100.0f, 25.0f, 300.0f, 400.0f) == 300.0f);
    TEST_CHECK(run, UIScrollbar_GetOffsetAt(100.0f, 100.0f, 300.0f, 10.0f) == 0.0f);
    TEST_CHECK(run, UIScrollbar_GetOffsetAt(100.0f, 25.0f, 0.0f, 10.0f) == 0.0f);
    TEST_CHECK(run, UIScrollbar_GetOffsetAt(100.0f, 25.0f, 300.0f, NAN) == 0.0f);
    for (float Offset = 0.0f; Offset <= 300.0f; Offset += 37.0f)
    {
        UIScrollbar_GetThumb(100.0f, 10.0f, 400.0f, 100.0f, Offset, &Start, &Length);
        TEST_CHECK(run, IsNear(UIScrollbar_GetOffsetAt(100.0f, Length, 300.0f, Start), Offset));
    }
}


// Tests: the area.
static void AnAreaPlacesItsContentAtItsOffset(TestRun* run)
{
    ScrollFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t AreaId = AddMainArea(run, &Fixture, UIScrollMode_Auto, UIScrollMode_Auto);
        uint64_t ContentId = AddSizedBox(run, &Fixture, AreaId, 300.0f, 400.0f);
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture.UI, ContentId), AREA_X, AREA_Y, 300.0f, 400.0f));
        Vector2 Content = { .x = 0.0f, .y = 0.0f };
        Vector2 View = { .x = 0.0f, .y = 0.0f };
        TEST_CHECK_SUCCESS(run, UIScrollable_GetSizes(&Fixture.UI.Screen, AreaId, &Content, &View));
        TEST_CHECK(run, (Content.x == 300.0f) && (Content.y == 400.0f) && (View.x == (AREA_WIDTH - BAR)) && (View.y == (AREA_HEIGHT - BAR)));

        TEST_CHECK_SUCCESS(run, UIScrollable_SetOffset(&Fixture.UI.Screen, AreaId, (Vector2){ .x = 30.0f, .y = 100.0f }));
        TestUI_Tick(run, &Fixture.UI);
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture.UI, ContentId), AREA_X - 30.0f, AREA_Y - 100.0f, 300.0f, 400.0f));
        // Past the range it stops at the end.
        TEST_CHECK_SUCCESS(run, UIScrollable_SetOffset(&Fixture.UI.Screen, AreaId, (Vector2){ .x = 9999.0f, .y = 9999.0f }));
        Vector2 Offset = GetOffset(run, &Fixture, AreaId);
        TEST_CHECK(run, (Offset.x == (300.0f - (AREA_WIDTH - BAR))) && (Offset.y == (400.0f - (AREA_HEIGHT - BAR))));
        TestUI_Tick(run, &Fixture.UI);
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture.UI, ContentId), AREA_X - Offset.x, AREA_Y - Offset.y, 300.0f, 400.0f));

        // Content that shrinks clamps the offset; a short page still fills the view.
        GetSizedBox(&Fixture, ContentId)->Size = (Vector2){ .x = 120.0f, .y = 140.0f };
        Widget_InvalidateMeasure(&GetSizedBox(&Fixture, ContentId)->Base);
        TestUI_Tick(run, &Fixture.UI);
        Offset = GetOffset(run, &Fixture, AreaId);
        TEST_CHECK(run, (Offset.x == 0.0f) && IsNear(Offset.y, 140.0f - AREA_HEIGHT + 0.0f));
        TestUI_Tick(run, &Fixture.UI);
        Rectangle Box = TestUI_GetPixelBox(run, &Fixture.UI, ContentId);
        TEST_CHECK(run, IsNear(Box.width, AREA_WIDTH - BAR) && IsNear(Box.height, 140.0f));
        GetSizedBox(&Fixture, ContentId)->Size = (Vector2){ .x = 50.0f, .y = 30.0f };
        Widget_InvalidateMeasure(&GetSizedBox(&Fixture, ContentId)->Base);
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture.UI, ContentId), AREA_X, AREA_Y, AREA_WIDTH, AREA_HEIGHT));

        // A view that grows clamps it too.
        GetSizedBox(&Fixture, ContentId)->Size = (Vector2){ .x = 100.0f, .y = 300.0f };
        Widget_InvalidateMeasure(&GetSizedBox(&Fixture, ContentId)->Base);
        TestUI_Tick(run, &Fixture.UI);
        TEST_CHECK_SUCCESS(run, UIScrollable_SetOffset(&Fixture.UI.Screen, AreaId, (Vector2){ .x = 0.0f, .y = 200.0f }));
        Widget* Area = NULL;
        UIScreen_TryGetWidgetById(&Fixture.UI.Screen, AreaId, &Area);
        UIPlacement Taller = UIPlacement_FromTopLeft(AREA_X, AREA_Y, AREA_WIDTH, 250.0f);
        TEST_CHECK_SUCCESS(run, Widget_SetPlacement(Area, &Taller));
        TestUI_Tick(run, &Fixture.UI);
        TEST_CHECK(run, IsNear(GetOffset(run, &Fixture, AreaId).y, 50.0f));
    }
    Deconstruct(run, &Fixture);
}

static void BarsShowByTheirModes(TestRun* run)
{
    ScrollFixture Fixture;
    if (Construct(run, &Fixture))
    {
        // Auto down, nothing across: a tall page shows the vertical bar, which runs the whole height.
        uint64_t AreaId = AddMainArea(run, &Fixture, UIScrollMode_Disabled, UIScrollMode_Auto);
        uint64_t ContentId = AddSizedBox(run, &Fixture, AreaId, 500.0f, 400.0f);
        TestUI_Ticks(run, &Fixture.UI, 2U);
        UIScrollAreaCapability* Area = GetArea(&Fixture, AreaId);
        TEST_CHECK(run, (Area != NULL) && Area->_isBarShown[UIScrollAxis_Vertical] && !Area->_isBarShown[UIScrollAxis_Horizontal]);
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture.UI, Area->_barIds[UIScrollAxis_Vertical]), AREA_X + AREA_WIDTH - BAR, AREA_Y,
            BAR, AREA_HEIGHT));
        Rectangle Hidden = TestUI_GetPixelBox(run, &Fixture.UI, Area->_barIds[UIScrollAxis_Horizontal]);
        TEST_CHECK(run, (Hidden.width == 0.0f) && (Hidden.height == 0.0f));
        // Not scrolling across, the content gets the view's width whatever it asks for.
        TEST_CHECK(run, IsNear(TestUI_GetPixelBox(run, &Fixture.UI, ContentId).width, AREA_WIDTH - BAR));

        // A page that fits shows no bar, and gets the whole box.
        GetSizedBox(&Fixture, ContentId)->Size = (Vector2){ .x = 50.0f, .y = 60.0f };
        Widget_InvalidateMeasure(&GetSizedBox(&Fixture, ContentId)->Base);
        TestUI_Ticks(run, &Fixture.UI, 2U);
        Area = GetArea(&Fixture, AreaId);
        TEST_CHECK(run, !Area->_isBarShown[UIScrollAxis_Vertical]);
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture.UI, ContentId), AREA_X, AREA_Y, AREA_WIDTH, AREA_HEIGHT));

        // Always: the bars show even then, and their thumbs fill their tracks.
        TEST_CHECK_SUCCESS(run, UIScrollable_SetModes(&Fixture.UI.Screen, AreaId, UIScrollMode_Always, UIScrollMode_Always));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        Area = GetArea(&Fixture, AreaId);
        TEST_CHECK(run, Area->_isBarShown[UIScrollAxis_Vertical] && Area->_isBarShown[UIScrollAxis_Horizontal]);
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture.UI, Area->_barIds[UIScrollAxis_Horizontal]), AREA_X,
            AREA_Y + AREA_HEIGHT - BAR, AREA_WIDTH - BAR, BAR));
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture.UI, ContentId), AREA_X, AREA_Y, AREA_WIDTH - BAR, AREA_HEIGHT - BAR));
        Widget* VerticalBar = NULL;
        UIScreen_TryGetWidgetById(&Fixture.UI.Screen, Area->_barIds[UIScrollAxis_Vertical], &VerticalBar);
        const UIScrollbarCapability* Bar = &((UIScrollbarLookBase*)(void*)VerticalBar)->Scrollbar;
        Rectangle Thumb;
        Rectangle Track = TestUI_GetPixelBox(run, &Fixture.UI, Area->_barIds[UIScrollAxis_Vertical]);
        TEST_CHECK(run, !UIScrollbarCapability_GetThumbBox(Bar, VerticalBar, Track, 0.0f, &Thumb));
        // The corner where both meet is the vertical bar's, but its thumb never enters it.
        TEST_CHECK(run, TestUI_IsBox(Thumb, AREA_X + AREA_WIDTH - BAR, AREA_Y, BAR, AREA_HEIGHT - BAR));

        // Auto both ways with content larger both ways: both bars, and the view less both.
        TEST_CHECK_SUCCESS(run, UIScrollable_SetModes(&Fixture.UI.Screen, AreaId, UIScrollMode_Auto, UIScrollMode_Auto));
        GetSizedBox(&Fixture, ContentId)->Size = (Vector2){ .x = 300.0f, .y = 95.0f };
        Widget_InvalidateMeasure(&GetSizedBox(&Fixture, ContentId)->Base);
        TestUI_Ticks(run, &Fixture.UI, 2U);
        Area = GetArea(&Fixture, AreaId);
        // 95 fits the 100 tall box alone, but the bar across leaves 90: the vertical bar shows because of the horizontal one.
        TEST_CHECK(run, Area->_isBarShown[UIScrollAxis_Vertical] && Area->_isBarShown[UIScrollAxis_Horizontal]);
        Vector2 Range = { .x = 0.0f, .y = 0.0f };
        TEST_CHECK_SUCCESS(run, UIScrollable_GetRange(&Fixture.UI.Screen, AreaId, &Range));
        TEST_CHECK(run, IsNear(Range.x, 300.0f - (AREA_WIDTH - BAR)) && IsNear(Range.y, 95.0f - (AREA_HEIGHT - BAR)));
    }
    Deconstruct(run, &Fixture);
}

static void TheContentIsMeasuredAtTheViewsWidthWhenOnlyDownScrolls(TestRun* run)
{
    ScrollFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t AreaId = AddMainArea(run, &Fixture, UIScrollMode_Disabled, UIScrollMode_Auto);
        uint64_t ColumnId = AddColumn(run, &Fixture, AreaId);
        UILabelOptions Label = UILabelOptions_Create(u8"one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen "
            "sixteen seventeen eighteen nineteen twenty twenty-one twenty-two twenty-three twenty-four twenty-five twenty-six twenty-seven "
            "twenty-eight twenty-nine thirty thirty-one thirty-two thirty-three thirty-four thirty-five thirty-six thirty-seven");
        Label.IsWrapped = true;
        uint64_t LabelId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateLabel(&Fixture.UI.Kit, &Fixture.UI.Screen, ColumnId, &Label, &LabelId));
        TestUI_Ticks(run, &Fixture.UI, 4U);
        // The text wraps at the view's width (less the bar its height calls for), so the page is taller than the box.
        Rectangle Box = TestUI_GetPixelBox(run, &Fixture.UI, LabelId);
        TEST_CHECK(run, Box.width <= (AREA_WIDTH - BAR + 0.01f));
        TEST_CHECK(run, Box.height > AREA_HEIGHT);
        TEST_CHECK(run, GetArea(&Fixture, AreaId)->_isBarShown[UIScrollAxis_Vertical]);

        // Scrolling across too, it is measured unbounded: one long line, and a bar across.
        TEST_CHECK_SUCCESS(run, UIScrollable_SetModes(&Fixture.UI.Screen, AreaId, UIScrollMode_Auto, UIScrollMode_Auto));
        TestUI_Ticks(run, &Fixture.UI, 3U);
        Box = TestUI_GetPixelBox(run, &Fixture.UI, LabelId);
        TEST_CHECK(run, Box.width > AREA_WIDTH);
        TEST_CHECK(run, Box.height < 40.0f);
        TEST_CHECK(run, GetArea(&Fixture, AreaId)->_isBarShown[UIScrollAxis_Horizontal]);

        // Down disabled and across scrolling: the content gets the view's height whatever it measures.
        uint64_t FlatId = AddArea(run, &Fixture, WIDGET_ID_INVALID, UIScrollMode_Auto, UIScrollMode_Disabled,
            UIPlacement_FromTopLeft(AREA_X, 300.0f, AREA_WIDTH, AREA_HEIGHT));
        uint64_t TallId = AddSizedBox(run, &Fixture, FlatId, 400.0f, 300.0f);
        TestUI_Ticks(run, &Fixture.UI, 3U);
        Box = TestUI_GetPixelBox(run, &Fixture.UI, TallId);
        TEST_CHECK(run, IsNear(Box.width, 400.0f) && IsNear(Box.height, AREA_HEIGHT - BAR));
    }
    Deconstruct(run, &Fixture);
}

/* What runs past the area's box is cut, for the pointer too. */
static void AnAreaCutsItsContent(TestRun* run)
{
    ScrollFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t AreaId = AddMainArea(run, &Fixture, UIScrollMode_Disabled, UIScrollMode_Auto);
        uint64_t ColumnId = AddColumn(run, &Fixture, AreaId);
        uint64_t ButtonId = AddButton(run, &Fixture, ColumnId, u8"tall", 300.0f);
        TestUI_Ticks(run, &Fixture.UI, 3U);
        Widget* Top = NULL;
        TEST_CHECK(run, UIScreen_TryGetTopmostAt(&Fixture.UI.Screen, (Vector2){ .x = 150.0f / TEST_UI_SURFACE_WIDTH, .y = 100.0f / TEST_UI_SURFACE_HEIGHT },
            &Top) && (Widget_GetId(Top) == ButtonId));
        Top = NULL;
        bool IsHit = UIScreen_TryGetTopmostAt(&Fixture.UI.Screen, (Vector2){ .x = 150.0f / TEST_UI_SURFACE_WIDTH, .y = 200.0f / TEST_UI_SURFACE_HEIGHT },
            &Top);
        TEST_CHECK(run, !IsHit || (Widget_GetId(Top) != ButtonId));
    }
    Deconstruct(run, &Fixture);
}

static void AnIdleAreaMeasuresNothing(TestRun* run)
{
    ScrollFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t AreaId = AddMainArea(run, &Fixture, UIScrollMode_Auto, UIScrollMode_Auto);
        uint64_t ContentId = AddSizedBox(run, &Fixture, AreaId, 300.0f, 95.0f); // Both bars, one because of the other.
        TestUI_Ticks(run, &Fixture.UI, 3U);
        SizedBox* Box = GetSizedBox(&Fixture, ContentId);
        size_t Before = Box->MeasureCount;
        TestUI_Ticks(run, &Fixture.UI, 100U);
        TEST_CHECK(run, GetSizedBox(&Fixture, ContentId)->MeasureCount == Before);
        // Scrolling moves the content without measuring it again.
        TEST_CHECK_SUCCESS(run, UIScrollable_ScrollBy(&Fixture.UI.Screen, AreaId, (Vector2){ .x = 20.0f, .y = 3.0f }));
        TestUI_Ticks(run, &Fixture.UI, 10U);
        TEST_CHECK(run, GetSizedBox(&Fixture, ContentId)->MeasureCount == Before);
        TEST_CHECK(run, IsNear(TestUI_GetPixelBox(run, &Fixture.UI, ContentId).x, AREA_X - 20.0f));
    }
    Deconstruct(run, &Fixture);
}

static void TheWheelScrollsAndGoesOnAtTheEnd(TestRun* run)
{
    ScrollFixture Fixture;
    if (Construct(run, &Fixture))
    {
        // An outer area holding a page with an inner area 100 tall in it, whose content is 300 tall.
        uint64_t OuterId = AddMainArea(run, &Fixture, UIScrollMode_Disabled, UIScrollMode_Auto);
        uint64_t PageId = AddColumn(run, &Fixture, OuterId);
        uint64_t Top = AddButton(run, &Fixture, PageId, u8"top", 40.0f);
        UIScrollAreaOptions Inner = UIScrollAreaOptions_Create();
        Inner.Element.Hint = UIStackHint_Fixed(60.0f);
        uint64_t InnerId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateScrollArea(&Fixture.UI.Kit, &Fixture.UI.Screen, PageId, &Inner, &InnerId));
        AddSizedBox(run, &Fixture, InnerId, 50.0f, 300.0f);
        AddButton(run, &Fixture, PageId, u8"bottom", 200.0f);
        TestUI_Ticks(run, &Fixture.UI, 4U);
        (void)Top;

        // Over the inner area the wheel scrolls it, a notch at a time, until its end; then the outer one.
        TestUI_MovePointer(&Fixture.UI, AREA_X + 20.0f, AREA_Y + 60.0f);
        TestUI_Tick(run, &Fixture.UI);
        TestUI_Wheel(&Fixture.UI, -1.0f);
        TestUI_Tick(run, &Fixture.UI);
        TEST_CHECK(run, GetOffset(run, &Fixture, InnerId).y == UI_SCROLLABLE_DEFAULT_WHEEL_STEP);
        TEST_CHECK(run, GetOffset(run, &Fixture, OuterId).y == 0.0f);
        TestUI_Wheel(&Fixture.UI, -10.0f);
        TestUI_Tick(run, &Fixture.UI);
        TEST_CHECK(run, GetOffset(run, &Fixture, InnerId).y == 240.0f);
        TEST_CHECK(run, GetOffset(run, &Fixture, OuterId).y == 0.0f);
        TestUI_Wheel(&Fixture.UI, -1.0f);
        TestUI_Tick(run, &Fixture.UI);
        TEST_CHECK(run, GetOffset(run, &Fixture, InnerId).y == 240.0f);
        TEST_CHECK(run, GetOffset(run, &Fixture, OuterId).y == UI_SCROLLABLE_DEFAULT_WHEEL_STEP);
        // Back up, over the inner area where it shows now (the page moved up with the outer one): the inner first.
        TestUI_MovePointer(&Fixture.UI, AREA_X + 20.0f, AREA_Y + 30.0f);
        TestUI_Tick(run, &Fixture.UI);
        TestUI_Wheel(&Fixture.UI, 1.0f);
        TestUI_Tick(run, &Fixture.UI);
        TEST_CHECK(run, GetOffset(run, &Fixture, InnerId).y == (240.0f - UI_SCROLLABLE_DEFAULT_WHEEL_STEP));

        // At the outer area's end too, the wheel is the screen's still: consumed, and nothing more happens.
        TEST_CHECK_SUCCESS(run, UIScrollable_SetOffset(&Fixture.UI.Screen, OuterId, (Vector2){ .x = 0.0f, .y = 9999.0f }));
        TestUI_Tick(run, &Fixture.UI);
        TestUI_MovePointer(&Fixture.UI, AREA_X + 20.0f, AREA_Y + 90.0f);
        TestUI_Tick(run, &Fixture.UI);
        Vector2 Before = GetOffset(run, &Fixture, OuterId);
        TestUI_Wheel(&Fixture.UI, -1.0f);
        TestUI_Tick(run, &Fixture.UI);
        TEST_CHECK(run, GetOffset(run, &Fixture, OuterId).y == Before.y);
        TEST_CHECK(run, GameInput_GetWheelDelta(&Fixture.UI.Input).y == 0.0f);
    }
    Deconstruct(run, &Fixture);
}

static void ShiftTurnsTheWheelAcross(TestRun* run)
{
    ScrollFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t AreaId = AddMainArea(run, &Fixture, UIScrollMode_Auto, UIScrollMode_Auto);
        AddSizedBox(run, &Fixture, AreaId, 600.0f, 600.0f);
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TestUI_MovePointer(&Fixture.UI, AREA_X + 20.0f, AREA_Y + 20.0f);
        TestUI_Tick(run, &Fixture.UI);
        TestUI_Press(&Fixture.UI, GameKey_LeftShift);
        TestUI_Wheel(&Fixture.UI, -2.0f);
        TestUI_Tick(run, &Fixture.UI);
        Vector2 Offset = GetOffset(run, &Fixture, AreaId);
        TEST_CHECK(run, (Offset.x == (2.0f * UI_SCROLLABLE_DEFAULT_WHEEL_STEP)) && (Offset.y == 0.0f));
        TestUI_Release(&Fixture.UI, GameKey_LeftShift);
        TestUI_Tick(run, &Fixture.UI);
        // A wheel that turns sideways by itself scrolls across too.
        Fixture.UI.Sample.WheelDelta = (Vector2){ .x = 1.0f, .y = 0.0f };
        TestUI_Tick(run, &Fixture.UI);
        TEST_CHECK(run, GetOffset(run, &Fixture, AreaId).x == UI_SCROLLABLE_DEFAULT_WHEEL_STEP);
        // An area that does not scroll across leaves Shift and the wheel to what lies above it.
        TEST_CHECK_SUCCESS(run, UIScrollable_SetModes(&Fixture.UI.Screen, AreaId, UIScrollMode_Disabled, UIScrollMode_Auto));
        TestUI_Tick(run, &Fixture.UI);
        TestUI_Press(&Fixture.UI, GameKey_LeftShift);
        TestUI_Wheel(&Fixture.UI, -1.0f);
        TestUI_Tick(run, &Fixture.UI);
        TEST_CHECK(run, GetOffset(run, &Fixture, AreaId).y == 0.0f);
    }
    Deconstruct(run, &Fixture);
}

static void TheWheelOverElementsScrollsAndNeverChangesASlider(TestRun* run)
{
    ScrollFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t AreaId = AddMainArea(run, &Fixture, UIScrollMode_Disabled, UIScrollMode_Auto);
        uint64_t PageId = AddColumn(run, &Fixture, AreaId);
        AddButton(run, &Fixture, PageId, u8"button", 40.0f);
        UISliderOptions Slider = UISliderOptions_Create();
        Slider.Value = 0.5;
        Slider.Element.Hint = UIStackHint_Fixed(30.0f);
        uint64_t SliderId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateSlider(&Fixture.UI.Kit, &Fixture.UI.Screen, PageId, &Slider, &SliderId));
        AddSizedBox(run, &Fixture, PageId, 50.0f, 500.0f);
        TestUI_Ticks(run, &Fixture.UI, 4U);

        // Over the button, then over the slider: the area scrolls, the slider's value stays.
        TestUI_MovePointer(&Fixture.UI, AREA_X + 30.0f, AREA_Y + 20.0f);
        TestUI_Tick(run, &Fixture.UI);
        TestUI_Wheel(&Fixture.UI, -0.25f);
        TestUI_Tick(run, &Fixture.UI);
        TEST_CHECK(run, GetOffset(run, &Fixture, AreaId).y == (0.25f * UI_SCROLLABLE_DEFAULT_WHEEL_STEP));
        TEST_CHECK_SUCCESS(run, UIScrollable_SetOffset(&Fixture.UI.Screen, AreaId, (Vector2){ .x = 0.0f, .y = 0.0f }));
        TestUI_Tick(run, &Fixture.UI);
        TestUI_MovePointer(&Fixture.UI, AREA_X + 30.0f, AREA_Y + 55.0f);
        TestUI_Tick(run, &Fixture.UI);
        TEST_CHECK(run, UIScreen_GetHoveredWidgetId(&Fixture.UI.Screen) == SliderId);
        TestUI_Wheel(&Fixture.UI, -1.0f);
        TestUI_Tick(run, &Fixture.UI);
        double Value = 0.0;
        TEST_CHECK_SUCCESS(run, UISlider_GetValue(&Fixture.UI.Screen, SliderId, &Value));
        TEST_CHECK(run, (Value == 0.5) && (GetOffset(run, &Fixture, AreaId).y == UI_SCROLLABLE_DEFAULT_WHEEL_STEP));
    }
    Deconstruct(run, &Fixture);
}

static void TheThumbDragsAndTheTrackPages(TestRun* run)
{
    ScrollFixture Fixture;
    if (Construct(run, &Fixture))
    {
        // Content 400 tall in a 100 tall box, nothing across: the track is 100, the thumb 25 long, the range 300.
        uint64_t AreaId = AddMainArea(run, &Fixture, UIScrollMode_Disabled, UIScrollMode_Auto);
        AddSizedBox(run, &Fixture, AreaId, 50.0f, 400.0f);
        TestUI_Ticks(run, &Fixture.UI, 3U);
        float BarX = AREA_X + AREA_WIDTH - (BAR * 0.5f);

        // A press on the thumb holds it where it was grabbed: a click changes nothing, a drag moves the content by its share.
        TestUI_MovePointer(&Fixture.UI, BarX, AREA_Y + 10.0f);
        TestUI_Tick(run, &Fixture.UI);
        TestUI_Press(&Fixture.UI, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture.UI);
        TEST_CHECK(run, GetOffset(run, &Fixture, AreaId).y == 0.0f);
        TestUI_MovePointer(&Fixture.UI, BarX, AREA_Y + 10.0f + 25.0f);
        TestUI_Tick(run, &Fixture.UI);
        TEST_CHECK(run, IsNear(GetOffset(run, &Fixture, AreaId).y, 100.0f));
        // Past the bar and past the window, the end exactly; then back.
        TestUI_MovePointer(&Fixture.UI, BarX + 300.0f, 900.0f);
        TestUI_Tick(run, &Fixture.UI);
        TEST_CHECK(run, GetOffset(run, &Fixture, AreaId).y == 300.0f);
        TestUI_MovePointer(&Fixture.UI, BarX, -500.0f);
        TestUI_Tick(run, &Fixture.UI);
        TEST_CHECK(run, GetOffset(run, &Fixture, AreaId).y == 0.0f);
        TestUI_Release(&Fixture.UI, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture.UI);

        // Held where one page brings the thumb under the pointer, it pages once and stops there.
        TestUI_MovePointer(&Fixture.UI, BarX, AREA_Y + 45.0f);
        TestUI_Tick(run, &Fixture.UI);
        TestUI_Press(&Fixture.UI, GameKey_MouseLeft);
        TestUI_Ticks(run, &Fixture.UI, (size_t)(1.0 / TEST_UI_TICK_SECONDS));
        TEST_CHECK(run, GetOffset(run, &Fixture, AreaId).y == 100.0f);
        TestUI_Release(&Fixture.UI, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture.UI);
        TEST_CHECK_SUCCESS(run, UIScrollable_SetOffset(&Fixture.UI.Screen, AreaId, (Vector2){ .x = 0.0f, .y = 0.0f }));
        TestUI_Tick(run, &Fixture.UI);

        // A press on the track below the thumb pages down at once, and held, again after the wait, until the thumb reaches the pointer.
        TestUI_MovePointer(&Fixture.UI, BarX, AREA_Y + 80.0f);
        TestUI_Tick(run, &Fixture.UI);
        TestUI_Press(&Fixture.UI, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture.UI);
        TEST_CHECK(run, GetOffset(run, &Fixture, AreaId).y == 100.0f);
        TestUI_Ticks(run, &Fixture.UI, (size_t)(UI_SCROLLBAR_PAGE_DELAY_SECONDS / TEST_UI_TICK_SECONDS) - 2U);
        TEST_CHECK(run, GetOffset(run, &Fixture, AreaId).y == 100.0f);
        TestUI_Ticks(run, &Fixture.UI, 4U);
        TEST_CHECK(run, GetOffset(run, &Fixture, AreaId).y == 200.0f);
        // The thumb is at 50..75 now and the pointer at 80: one more page, then the thumb is under it and it stops.
        TestUI_Ticks(run, &Fixture.UI, (size_t)(1.0 / TEST_UI_TICK_SECONDS));
        TEST_CHECK(run, GetOffset(run, &Fixture, AreaId).y == 300.0f);
        TestUI_Release(&Fixture.UI, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture.UI);

        // Above the thumb it pages up; a quick click pages once.
        TestUI_MovePointer(&Fixture.UI, BarX, AREA_Y + 5.0f);
        TestUI_Tick(run, &Fixture.UI);
        TestUI_Press(&Fixture.UI, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture.UI);
        TestUI_Release(&Fixture.UI, GameKey_MouseLeft);
        TestUI_Ticks(run, &Fixture.UI, (size_t)(1.0 / TEST_UI_TICK_SECONDS));
        TEST_CHECK(run, GetOffset(run, &Fixture, AreaId).y == 200.0f);

        // The wheel over the bar scrolls its target.
        TestUI_Wheel(&Fixture.UI, 1.0f);
        TestUI_Tick(run, &Fixture.UI);
        TEST_CHECK(run, GetOffset(run, &Fixture, AreaId).y == (200.0f - UI_SCROLLABLE_DEFAULT_WHEEL_STEP));
    }
    Deconstruct(run, &Fixture);
}

static void ABarWithNothingToScrollDoesNothing(TestRun* run)
{
    ScrollFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIScrollbarOptions Options = UIScrollbarOptions_Create();
        Options.Element.HasPlacement = true;
        Options.Element.Placement = UIPlacement_FromTopLeft(500.0f, 50.0f, 10.0f, 100.0f);
        uint64_t BarId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateScrollbar(&Fixture.UI.Kit, &Fixture.UI.Screen, WIDGET_ID_INVALID, &Options, &BarId));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        uint64_t Target = 77U;
        TEST_CHECK_SUCCESS(run, UIScrollbar_GetTarget(&Fixture.UI.Screen, BarId, &Target));
        TEST_CHECK(run, Target == WIDGET_ID_INVALID);
        // Bound to nothing: a press does nothing, the wheel goes on.
        TestUI_Click(run, &Fixture.UI, 505.0f, 140.0f);
        TestUI_Wheel(&Fixture.UI, -1.0f);
        TestUI_Tick(run, &Fixture.UI);

        // Bound to an area, it moves it; its target gone, nothing again.
        uint64_t AreaId = AddMainArea(run, &Fixture, UIScrollMode_Disabled, UIScrollMode_Auto);
        AddSizedBox(run, &Fixture, AreaId, 50.0f, 400.0f);
        TEST_CHECK_SUCCESS(run, UIScrollbar_SetTarget(&Fixture.UI.Screen, BarId, AreaId));
        TestUI_Ticks(run, &Fixture.UI, 3U);
        TestUI_Click(run, &Fixture.UI, 505.0f, 140.0f);
        TEST_CHECK(run, GetOffset(run, &Fixture, AreaId).y == AREA_HEIGHT); // A page of the area's view: 100 tall, no bar across.
        Widget* Area = NULL;
        UIScreen_TryGetWidgetById(&Fixture.UI.Screen, AreaId, &Area);
        TEST_CHECK_SUCCESS(run, Widget_Destroy(Area));
        TestUI_Tick(run, &Fixture.UI);
        TestUI_Click(run, &Fixture.UI, 505.0f, 140.0f);
        TEST_CHECK_ERROR(run, UIScrollbar_SetTarget(&Fixture.UI.Screen, AreaId, BarId), ErrorCode_InvalidOperation);
    }
    Deconstruct(run, &Fixture);
}

static void FocusByTheKeyboardScrollsIntoViewAndAPressDoesNot(TestRun* run)
{
    ScrollFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t AreaId = AddMainArea(run, &Fixture, UIScrollMode_Disabled, UIScrollMode_Auto);
        uint64_t PageId = AddColumn(run, &Fixture, AreaId);
        uint64_t Buttons[8];
        for (size_t Index = 0U; Index < 8U; Index++)
        {
            Buttons[Index] = AddButton(run, &Fixture, PageId, u8"b", 40.0f);
        }
        TestUI_Ticks(run, &Fixture.UI, 4U);

        // Tab to the fourth button (120..160 of the page): the view (100 tall) scrolls to end at its bottom.
        TestUI_Tap(run, &Fixture.UI, GameKey_Tab);
        for (size_t Step = 0U; Step < 3U; Step++)
        {
            TestUI_Tap(run, &Fixture.UI, GameKey_Tab);
        }
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == Buttons[3]);
        TEST_CHECK(run, IsNear(GetOffset(run, &Fixture, AreaId).y, 60.0f));
        // Back to the first: its top.
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.UI.Screen, NULL));
        Widget* First = NULL;
        UIScreen_TryGetWidgetById(&Fixture.UI.Screen, Buttons[0], &First);
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.UI.Screen, First));
        TEST_CHECK(run, GetOffset(run, &Fixture, AreaId).y == 0.0f);
        TestUI_Tick(run, &Fixture.UI);

        // A press on the half-shown third button focuses it and leaves the view where it is.
        TestUI_Click(run, &Fixture.UI, AREA_X + 20.0f, AREA_Y + 90.0f);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == Buttons[2]);
        TEST_CHECK(run, GetOffset(run, &Fixture, AreaId).y == 0.0f);

        // A widget outside the area, or not inside it, is refused by the reveal.
        TEST_CHECK_ERROR(run, UIScrollable_RevealWidget(&Fixture.UI.Screen, AreaId, AreaId), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIScrollable_RevealWidget(&Fixture.UI.Screen, Buttons[0], Buttons[1]), ErrorCode_InvalidOperation);
        TEST_CHECK_SUCCESS(run, UIScrollable_RevealWidget(&Fixture.UI.Screen, AreaId, Buttons[7]));
        TEST_CHECK(run, IsNear(GetOffset(run, &Fixture, AreaId).y, 220.0f));
    }
    Deconstruct(run, &Fixture);
}

static void NestedAreasEachRevealInOneTick(TestRun* run)
{
    ScrollFixture Fixture;
    if (Construct(run, &Fixture))
    {
        // The outer page: 300 of space, an inner area 60 tall, more space. The inner page: 200 of space, then a button.
        uint64_t OuterId = AddMainArea(run, &Fixture, UIScrollMode_Disabled, UIScrollMode_Auto);
        uint64_t PageId = AddColumn(run, &Fixture, OuterId);
        AddSizedBox(run, &Fixture, PageId, 50.0f, 300.0f);
        UIScrollAreaOptions Inner = UIScrollAreaOptions_Create();
        Inner.Element.Hint = UIStackHint_Fixed(60.0f);
        uint64_t InnerId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateScrollArea(&Fixture.UI.Kit, &Fixture.UI.Screen, PageId, &Inner, &InnerId));
        AddSizedBox(run, &Fixture, PageId, 50.0f, 300.0f);
        uint64_t InnerPageId = AddColumn(run, &Fixture, InnerId);
        AddSizedBox(run, &Fixture, InnerPageId, 50.0f, 200.0f);
        uint64_t ButtonId = AddButton(run, &Fixture, InnerPageId, u8"deep", 30.0f);
        TestUI_Ticks(run, &Fixture.UI, 5U);

        Widget* Button = NULL;
        UIScreen_TryGetWidgetById(&Fixture.UI.Screen, ButtonId, &Button);
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.UI.Screen, Button));
        // The inner area shows the button at its bottom (200..230 in 60: 170), the outer the inner area's view (300..360 in 100: 260).
        TEST_CHECK(run, IsNear(GetOffset(run, &Fixture, InnerId).y, 170.0f));
        TEST_CHECK(run, IsNear(GetOffset(run, &Fixture, OuterId).y, 260.0f));
        TestUI_Tick(run, &Fixture.UI);
        Rectangle Box = TestUI_GetPixelBox(run, &Fixture.UI, ButtonId);
        TEST_CHECK(run, (Box.y >= (AREA_Y - 0.01f)) && ((Box.y + Box.height) <= (AREA_Y + AREA_HEIGHT + 0.01f)));
    }
    Deconstruct(run, &Fixture);
}

static void PageKeysScrollWhatTheFocusLeavesThem(TestRun* run)
{
    ScrollFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t AreaId = AddMainArea(run, &Fixture, UIScrollMode_Disabled, UIScrollMode_Auto);
        uint64_t PageId = AddColumn(run, &Fixture, AreaId);
        uint64_t ButtonId = AddButton(run, &Fixture, PageId, u8"b", 40.0f);
        AddSizedBox(run, &Fixture, PageId, 50.0f, 400.0f);
        TestUI_Ticks(run, &Fixture.UI, 4U);
        Widget* Button = NULL;
        UIScreen_TryGetWidgetById(&Fixture.UI.Screen, ButtonId, &Button);
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.UI.Screen, Button));
        TestUI_Tap(run, &Fixture.UI, GameKey_PageDown);
        TEST_CHECK(run, GetOffset(run, &Fixture, AreaId).y == AREA_HEIGHT);
        TestUI_Press(&Fixture.UI, GameKey_PageDown);
        TestUI_Tick(run, &Fixture.UI);
        TestUI_Repeat(&Fixture.UI, GameKey_PageDown);
        TestUI_Tick(run, &Fixture.UI);
        TestUI_Release(&Fixture.UI, GameKey_PageDown);
        TestUI_Tick(run, &Fixture.UI);
        TEST_CHECK(run, GetOffset(run, &Fixture, AreaId).y == 3.0f * AREA_HEIGHT);
        TestUI_Tap(run, &Fixture.UI, GameKey_PageUp);
        TEST_CHECK(run, GetOffset(run, &Fixture, AreaId).y == 2.0f * AREA_HEIGHT);
        // At the end the key goes on: here, to nobody, so it is not consumed.
        TEST_CHECK_SUCCESS(run, UIScrollable_SetOffset(&Fixture.UI.Screen, AreaId, (Vector2){ .x = 0.0f, .y = 0.0f }));
        TestUI_Press(&Fixture.UI, GameKey_PageUp);
        TestUI_Tick(run, &Fixture.UI);
        TEST_CHECK(run, GameInput_IsKeyPressed(&Fixture.UI.Input, GameKey_PageUp));
        TestUI_Release(&Fixture.UI, GameKey_PageUp);
        TestUI_Tick(run, &Fixture.UI);
    }
    Deconstruct(run, &Fixture);
}

static void StaleIdsAndBadValuesAreRefused(TestRun* run)
{
    ScrollFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t AreaId = AddMainArea(run, &Fixture, UIScrollMode_Auto, UIScrollMode_Auto);
        AddSizedBox(run, &Fixture, AreaId, 600.0f, 600.0f);
        TestUI_Ticks(run, &Fixture.UI, 2U);
        UIScreen* Screen = &Fixture.UI.Screen;
        Vector2 Out = { .x = 0.0f, .y = 0.0f };
        TEST_CHECK_ERROR(run, UIScrollable_SetOffset(Screen, AreaId, (Vector2){ .x = NAN, .y = 0.0f }), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIScrollable_ScrollBy(Screen, AreaId, (Vector2){ .x = 0.0f, .y = INFINITY }), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIScrollable_EnsureVisible(Screen, AreaId, (Rectangle){ .x = 0.0f, .y = 0.0f, .width = -2.0f, .height = 1.0f }),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIScrollable_SetModes(Screen, AreaId, (UIScrollMode)9, UIScrollMode_Auto), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIScrollable_SetWheelStep(Screen, AreaId, 0.0f), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIScrollable_SetWheelStep(Screen, AreaId, NAN), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, GetOffset(run, &Fixture, AreaId).x == 0.0f);
        TEST_CHECK_SUCCESS(run, UIScrollable_EnsureVisible(Screen, AreaId, (Rectangle){ .x = 400.0f, .y = 500.0f, .width = 20.0f, .height = 20.0f }));
        Out = GetOffset(run, &Fixture, AreaId);
        TEST_CHECK(run, IsNear(Out.x, 420.0f - (AREA_WIDTH - BAR)) && IsNear(Out.y, 520.0f - (AREA_HEIGHT - BAR)));
        TEST_CHECK_SUCCESS(run, UIScrollable_SetWheelStep(Screen, AreaId, 10.0f));

        // A widget that holds no scrollable, and a stale id.
        uint64_t ButtonId = AddButton(run, &Fixture, WIDGET_ID_INVALID, u8"b", 20.0f);
        TEST_CHECK_ERROR(run, UIScrollable_GetOffset(Screen, ButtonId, &Out), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIScrollbar_GetTarget(Screen, ButtonId, &(uint64_t){ 0U }), ErrorCode_InvalidOperation);
        Widget* Area = NULL;
        UIScreen_TryGetWidgetById(Screen, AreaId, &Area);
        TEST_CHECK_SUCCESS(run, Widget_Destroy(Area));
        TEST_CHECK_ERROR(run, UIScrollable_GetRange(Screen, AreaId, &Out), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIScrollable_GetSizes(Screen, AreaId, &Out, NULL), ErrorCode_InvalidOperation);
        TestUI_Tick(run, &Fixture.UI);

        // Bad options are refused, with nothing left behind.
        UIScrollAreaOptions Bad = UIScrollAreaOptions_Create();
        Bad.WheelStep = -1.0f;
        uint64_t Id = 5U;
        TEST_CHECK_ERROR(run, UIKit_CreateScrollArea(&Fixture.UI.Kit, Screen, WIDGET_ID_INVALID, &Bad, &Id), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, Id == WIDGET_ID_INVALID);
        UIScrollbarOptions BadBar = UIScrollbarOptions_Create();
        BadBar.Axis = UIScrollAxis_Count;
        TEST_CHECK_ERROR(run, UIKit_CreateScrollbar(&Fixture.UI.Kit, Screen, WIDGET_ID_INVALID, &BadBar, &Id), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, UIScreen_GetWidgetCount(Screen) == 1U); // The button.
    }
    Deconstruct(run, &Fixture);
}


// Public functions.
void UIScrollTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "the_offset_showing_a_span_moves_as_little_as_it_takes", TheOffsetShowingASpanMovesAsLittleAsItTakes },
        { "the_offset_stays_in_its_range", TheOffsetStaysInItsRange },
        { "the_thumbs_arithmetic", TheThumbsArithmetic },
        { "an_area_places_its_content_at_its_offset", AnAreaPlacesItsContentAtItsOffset },
        { "bars_show_by_their_modes", BarsShowByTheirModes },
        { "the_content_is_measured_at_the_views_width_when_only_down_scrolls", TheContentIsMeasuredAtTheViewsWidthWhenOnlyDownScrolls },
        { "an_idle_area_measures_nothing", AnIdleAreaMeasuresNothing },
        { "an_area_cuts_its_content", AnAreaCutsItsContent },
        { "the_wheel_scrolls_and_goes_on_at_the_end", TheWheelScrollsAndGoesOnAtTheEnd },
        { "shift_turns_the_wheel_across", ShiftTurnsTheWheelAcross },
        { "the_wheel_over_elements_scrolls_and_never_changes_a_slider", TheWheelOverElementsScrollsAndNeverChangesASlider },
        { "the_thumb_drags_and_the_track_pages", TheThumbDragsAndTheTrackPages },
        { "a_bar_with_nothing_to_scroll_does_nothing", ABarWithNothingToScrollDoesNothing },
        { "focus_by_the_keyboard_scrolls_into_view_and_a_press_does_not", FocusByTheKeyboardScrollsIntoViewAndAPressDoesNot },
        { "nested_areas_each_reveal_in_one_tick", NestedAreasEachRevealInOneTick },
        { "page_keys_scroll_what_the_focus_leaves_them", PageKeysScrollWhatTheFocusLeavesThem },
        { "stale_ids_and_bad_values_are_refused", StaleIdsAndBadValuesAreRefused },
    };
    TestRun_RunSuite(run, "UIScroll", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
