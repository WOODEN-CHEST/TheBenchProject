#include "TestSuites.h"
#include "TestUI.h"
#include "UIKit.h"
#include "UITooltip.h"
#include <math.h>
#include <string.h>


// Macros.
#define COMPOUND_THEME \
    "{\"name\": \"compounds\", \"font\": \"mono\", \"text_size\": 16, \"elements\": {" \
    "\"label\": {\"text\": {\"normal\": \"#ffffff\"}}," \
    "\"slider\": {\"min_size\": [64, 24], \"thumb_size\": [20, 20], \"track_thickness\": 4, \"track\": {\"kind\": \"flat\"}, \"fill\": {\"kind\": \"flat\"}, \"thumb\": {\"kind\": \"flat\"}}," \
    "\"toggle\": {\"box_size\": [16, 16], \"spacing\": 6, \"min_size\": [10, 20], \"box\": {\"kind\": \"flat\"}, \"check\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#e0e0e0\"}}," \
    "\"property_box\": {\"padding\": [6, 3], \"min_size\": [60, 22], \"box\": {\"kind\": \"flat\"}, \"fill\": {\"kind\": \"flat\"}," \
    "\"selection\": {\"kind\": \"flat\"}, \"caret\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#ffffff\"}, \"unit\": {\"normal\": \"#aaaaaa\"}}," \
    "\"labelled\": {\"spacing\": 10}, \"form\": {\"padding\": [5, 4], \"spacing\": 6}, \"compound_slider\": {\"spacing\": 6, \"box_width\": 64}," \
    "\"vector_row\": {\"spacing\": 4, \"tag_width\": 16, \"x_tag\": {\"kind\": \"flat\"}, \"y_tag\": {\"kind\": \"flat\"}, \"z_tag\": {\"kind\": \"flat\"}," \
    "\"w_tag\": {\"kind\": \"flat\"}, \"tag_text\": {\"normal\": \"#ffffff\"}}}}"


// Static functions: helpers.
static bool Construct(TestRun* run, TestUIFixture* fixture)
{
    return TestUI_Construct(run, fixture) && TestUI_SetTheme(run, fixture, COMPOUND_THEME);
}

static UIElementOptions PlacedAt(float x, float y, float width, float height)
{
    UIElementOptions Element = UIElementOptions_Create();
    Element.HasPlacement = true;
    Element.Placement = UIPlacement_FromTopLeft(x, y, width, height);
    return Element;
}

static double Get(TestRun* run, TestUIFixture* fixture, uint64_t id)
{
    double Value = NAN;
    TEST_CHECK_SUCCESS(run, UIValue_Get(&fixture->Screen, id, &Value));
    return Value;
}

static bool IsAbout(double value, double expected)
{
    return fabs(value - expected) <= (1e-5 * fmax(1.0, fabs(expected)));
}

static void TypeText(TestRun* run, TestUIFixture* fixture, const char* text)
{
    for (const char* Cursor = text; *Cursor != 0; Cursor++)
    {
        fixture->Sample.TextCodepoints[0] = (uint32_t)(unsigned char)*Cursor;
        fixture->Sample.TextCodepointCount = 1U;
        TestUI_Tick(run, fixture);
    }
}

static size_t CountEvents(const TestUIEvents* events, uint64_t id, UIElementEvent event)
{
    size_t Count = 0U;
    for (size_t Index = 0U; Index < events->Count; Index++)
    {
        Count += ((events->Events[Index].WidgetId == id) && (events->Events[Index].WidgetEventId == (int32_t)event)) ? 1U : 0U;
    }
    return Count;
}

static size_t CountValueEvents(const TestUIEvents* events, uint64_t id)
{
    return CountEvents(events, id, UIElementEvent_Changed) + CountEvents(events, id, UIElementEvent_Committed)
        + CountEvents(events, id, UIElementEvent_Cancelled);
}

/* A labelled toggle (its label's text given) at a place, its toggle's id given back. */
static uint64_t AddLabelledToggle(TestRun* run, TestUIFixture* fixture, uint64_t parentId, const unsigned char* text, UILabelledLayout layout,
    UIElementOptions element, uint64_t* outToggleId)
{
    UILabelledOptions Options = UILabelledOptions_Create(text);
    Options.Layout = layout;
    Options.Element = element;
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIKit_CreateLabelled(&fixture->Kit, &fixture->Screen, parentId, &Options, &Id));
    UIToggleOptions Toggle = UIToggleOptions_Create(u8"on");
    TEST_CHECK_SUCCESS(run, UIKit_CreateToggle(&fixture->Kit, &fixture->Screen, Id, &Toggle, outToggleId));
    return Id;
}


// Tests: labelled elements and forms.
static void ALabelledElementPlacesItsLabelAndElement(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t ToggleId = WIDGET_ID_INVALID;
        uint64_t Id = AddLabelledToggle(run, &Fixture, WIDGET_ID_INVALID, u8"Name", UILabelledLayout_Across, PlacedAt(100.0f, 100.0f, 300.0f, 40.0f),
            &ToggleId);
        TestUI_Ticks(run, &Fixture, 3U);
        uint64_t LabelId = WIDGET_ID_INVALID;
        uint64_t ElementId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UILabelled_GetLabelId(&Fixture.Screen, Id, &LabelId));
        TEST_CHECK_SUCCESS(run, UILabelled_GetElementId(&Fixture.Screen, Id, &ElementId));
        TEST_CHECK(run, (ElementId == ToggleId) && (LabelId != WIDGET_ID_INVALID));
        // Across: the label at the left as wide as its text (four mono glyphs of 8), centred down; the element after the spacing, filling.
        Rectangle Label = TestUI_GetPixelBox(run, &Fixture, LabelId);
        Rectangle Element = TestUI_GetPixelBox(run, &Fixture, ToggleId);
        TEST_CHECK(run, TestUI_IsSamePixel(Label.x, 100.0f) && TestUI_IsSamePixel(Label.width, 32.0f));
        TEST_CHECK(run, TestUI_IsSamePixel(Label.y + (Label.height * 0.5f), 120.0f));
        TEST_CHECK(run, TestUI_IsBox(Element, 142.0f, 100.0f, 258.0f, 40.0f));
        // Stacked: the label on top, the element below it as wide as the row.
        TEST_CHECK_SUCCESS(run, UILabelled_SetLayout(&Fixture.Screen, Id, UILabelledLayout_Stacked));
        TestUI_Ticks(run, &Fixture, 2U);
        Label = TestUI_GetPixelBox(run, &Fixture, LabelId);
        Element = TestUI_GetPixelBox(run, &Fixture, ToggleId);
        TEST_CHECK(run, TestUI_IsSamePixel(Label.y, 100.0f) && TestUI_IsSamePixel(Element.x, 100.0f) && TestUI_IsSamePixel(Element.width, 300.0f));
        TEST_CHECK(run, TestUI_IsSamePixel(Element.y, Label.y + Label.height + 10.0f));
        // A label column of its own width.
        TEST_CHECK_SUCCESS(run, UILabelled_SetLayout(&Fixture.Screen, Id, UILabelledLayout_Across));
        TEST_CHECK_SUCCESS(run, UILabelled_SetLabelWidth(&Fixture.Screen, Id, true, 90.0f));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, TestUI_IsSamePixel(TestUI_GetPixelBox(run, &Fixture, ToggleId).x, 200.0f));
        TEST_CHECK_SUCCESS(run, UILabelled_SetText(&Fixture.Screen, Id, u8"Longer name"));
        TEST_CHECK_ERROR(run, UILabelled_SetLayout(&Fixture.Screen, Id, (UILabelledLayout)9), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UILabelled_SetLabelWidth(&Fixture.Screen, Id, true, -1.0f), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UILabelled_SetText(&Fixture.Screen, ToggleId, u8"x"), ErrorCode_InvalidOperation);
        // The element keeps its own id and functions.
        TEST_CHECK_SUCCESS(run, UIToggle_SetState(&Fixture.Screen, ToggleId, UIToggleState_On));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void AFormsLabelsLineUpOnTheWidest(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIFormOptions Options = UIFormOptions_Create();
        Options.Element = PlacedAt(100.0f, 50.0f, 400.0f, 300.0f);
        uint64_t FormId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateForm(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &FormId));
        uint64_t Toggles[3] = { WIDGET_ID_INVALID, WIDGET_ID_INVALID, WIDGET_ID_INVALID };
        AddLabelledToggle(run, &Fixture, FormId, u8"A", UILabelledLayout_Across, UIElementOptions_Create(), &Toggles[0]);
        AddLabelledToggle(run, &Fixture, FormId, u8"Longest", UILabelledLayout_Across, UIElementOptions_Create(), &Toggles[1]);
        UILabelOptions Plain = UILabelOptions_Create(u8"a row of its own");
        uint64_t PlainId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateLabel(&Fixture.Kit, &Fixture.Screen, FormId, &Plain, &PlainId));
        AddLabelledToggle(run, &Fixture, FormId, u8"Mid", UILabelledLayout_Across, UIElementOptions_Create(), &Toggles[2]);
        TestUI_Ticks(run, &Fixture, 4U);
        // Every labelled row's element starts after the widest label (7 glyphs: 56) and the spacing, inside the padding.
        bool IsLinedUp = true;
        for (size_t Index = 0U; Index < 3U; Index++)
        {
            IsLinedUp = IsLinedUp && TestUI_IsSamePixel(TestUI_GetPixelBox(run, &Fixture, Toggles[Index]).x, 105.0f + 56.0f + 10.0f);
        }
        TEST_CHECK(run, IsLinedUp);
        Rectangle Row = TestUI_GetPixelBox(run, &Fixture, PlainId);
        TEST_CHECK(run, TestUI_IsSamePixel(Row.x, 105.0f));
        Rectangle First = TestUI_GetPixelBox(run, &Fixture, Toggles[0]);
        TEST_CHECK(run, TestUI_IsSamePixel(First.y, 54.0f)); // The padding's top.
        // A longer label widens the column for every row.
        Widget* Toggle = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Toggles[0], &Toggle)))
        {
            TEST_CHECK_SUCCESS(run, UILabelled_SetText(&Fixture.Screen, Widget_GetId(Widget_GetParent(Toggle)), u8"Much longer"));
        }
        TestUI_Ticks(run, &Fixture, 4U);
        TEST_CHECK(run, TestUI_IsSamePixel(TestUI_GetPixelBox(run, &Fixture, Toggles[2]).x, 105.0f + 88.0f + 10.0f));
    }
    TestUI_Deconstruct(run, &Fixture);
}


/* A row moved out of its form stops using the form's label width: its own label's width counts again. */
static void ARowMovedOutOfItsFormUsesItsOwnLabelWidth(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIFormOptions Options = UIFormOptions_Create();
        Options.Element = PlacedAt(100.0f, 50.0f, 400.0f, 200.0f);
        uint64_t FormId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateForm(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &FormId));
        uint64_t Toggles[2] = { WIDGET_ID_INVALID, WIDGET_ID_INVALID };
        uint64_t RowId = AddLabelledToggle(run, &Fixture, FormId, u8"A", UILabelledLayout_Across, UIElementOptions_Create(), &Toggles[0]);
        AddLabelledToggle(run, &Fixture, FormId, u8"Longest", UILabelledLayout_Across, UIElementOptions_Create(), &Toggles[1]);
        TestUI_Ticks(run, &Fixture, 4U);
        TEST_CHECK(run, TestUI_IsSamePixel(TestUI_GetPixelBox(run, &Fixture, Toggles[0]).x - TestUI_GetPixelBox(run, &Fixture, RowId).x, 56.0f + 10.0f));
        Widget* Row = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, RowId, &Row)))
        {
            TEST_CHECK_SUCCESS(run, Widget_MoveTo(Row, NULL));
            UIPlacement Placement = UIPlacement_FromTopLeft(100.0f, 300.0f, 400.0f, 24.0f);
            TEST_CHECK_SUCCESS(run, Widget_SetPlacement(Row, &Placement));
        }
        TestUI_Ticks(run, &Fixture, 4U);
        TEST_CHECK(run, TestUI_IsSamePixel(TestUI_GetPixelBox(run, &Fixture, Toggles[0]).x - TestUI_GetPixelBox(run, &Fixture, RowId).x, 8.0f + 10.0f));
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Tests: compound sliders.
static uint64_t AddCompound(TestRun* run, TestUIFixture* fixture, uint64_t* outSliderId, uint64_t* outBoxId)
{
    UICompoundSliderOptions Options = UICompoundSliderOptions_Create();
    Options.Element = PlacedAt(100.0f, 100.0f, 400.0f, 24.0f);
    Options.Value = UIValueOptions_CreateRange(0.0, 10.0, 5.0);
    Options.Name = u8"Size";
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIKit_CreateCompoundSlider(&fixture->Kit, &fixture->Screen, WIDGET_ID_INVALID, &Options, &Id));
    TEST_CHECK_SUCCESS(run, UICompoundSlider_GetPartIds(&fixture->Screen, Id, NULL, outSliderId, outBoxId));
    TestUI_Ticks(run, fixture, 3U);
    return Id;
}

static void ACompoundSliderKeepsItsPartsInStep(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t SliderId = WIDGET_ID_INVALID;
        uint64_t BoxId = WIDGET_ID_INVALID;
        uint64_t Id = AddCompound(run, &Fixture, &SliderId, &BoxId);
        // Code on the compound moves both parts.
        TEST_CHECK_SUCCESS(run, UIValue_Set(&Fixture.Screen, Id, 7.0));
        TEST_CHECK(run, (Get(run, &Fixture, SliderId) == 7.0) && (Get(run, &Fixture, BoxId) == 7.0));
        // Code on a part moves the other and the compound.
        TEST_CHECK_SUCCESS(run, UIValue_Set(&Fixture.Screen, SliderId, 2.0));
        TEST_CHECK(run, (Get(run, &Fixture, Id) == 2.0) && (Get(run, &Fixture, BoxId) == 2.0));
        TEST_CHECK(run, Events.Count == 0U); // Code posts nothing.

        // Dragging the slider: the box follows at once, and the events are the compound's alone.
        Rectangle Slider = TestUI_GetPixelBox(run, &Fixture, SliderId);
        float Start = Slider.x + 10.0f + (0.2f * (Slider.width - 20.0f));
        TestUI_MovePointer(&Fixture, Start, Slider.y + 12.0f);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_MovePointer(&Fixture, Slider.x + 10.0f + (0.6f * (Slider.width - 20.0f)), Slider.y + 12.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsAbout(Get(run, &Fixture, BoxId), 6.0) && IsAbout(Get(run, &Fixture, Id), 6.0));
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (CountEvents(&Events, Id, UIElementEvent_Changed) == 1U) && (CountEvents(&Events, Id, UIElementEvent_Committed) == 1U));
        TEST_CHECK(run, (CountValueEvents(&Events, SliderId) == 0U) && (CountValueEvents(&Events, BoxId) == 0U));

        // Typing in the box: the slider follows, one Changed and one Committed under the compound's id.
        size_t Count = Events.Count;
        Rectangle Box = TestUI_GetPixelBox(run, &Fixture, BoxId);
        TestUI_Click(run, &Fixture, Box.x + (Box.width * 0.5f), Box.y + (Box.height * 0.5f));
        TypeText(run, &Fixture, "3");
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, (Get(run, &Fixture, SliderId) == 3.0) && (Get(run, &Fixture, Id) == 3.0));
        TEST_CHECK(run, (CountEvents(&Events, Id, UIElementEvent_Committed) == 2U) && (Events.Count - Count == 2U));

        // Settings set on the compound reach both parts.
        TEST_CHECK_SUCCESS(run, UIValue_SetRange(&Fixture.Screen, Id, true, 0.0, true, 2.0));
        TEST_CHECK(run, (Get(run, &Fixture, SliderId) == 2.0) && (Get(run, &Fixture, BoxId) == 2.0));
        UIValueSettings Settings;
        TEST_CHECK_SUCCESS(run, UIValue_GetSettings(&Fixture.Screen, BoxId, &Settings));
        TEST_CHECK(run, Settings.HasMaximum && (Settings.Maximum == 2.0));
        TEST_CHECK_SUCCESS(run, UIValue_SetMixed(&Fixture.Screen, Id, true));
        bool IsMixed = false;
        TEST_CHECK_SUCCESS(run, UIValue_IsMixed(&Fixture.Screen, BoxId, &IsMixed));
        TEST_CHECK(run, IsMixed);
        TEST_CHECK_SUCCESS(run, UIValue_SetIsEnabled(&Fixture.Screen, Id, false));
        Widget* Part = NULL;
        TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, SliderId, &Part) && !Widget_IsFocusable(Part));
        TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, BoxId, &Part) && !Widget_IsFocusable(Part));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ACompoundScalesAsOneAndLendsItsHoverText(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t SliderId = WIDGET_ID_INVALID;
        uint64_t BoxId = WIDGET_ID_INVALID;
        uint64_t Id = AddCompound(run, &Fixture, &SliderId, &BoxId);
        TEST_CHECK_SUCCESS(run, UIElement_SetScale(&Fixture.Screen, Id, 2.0f));
        TestUI_Ticks(run, &Fixture, 2U);
        Widget* Part = NULL;
        TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, BoxId, &Part) && (Widget_GetEffectiveScale(Part) == 2.0f));
        TEST_CHECK_SUCCESS(run, UITooltip_Set(&Fixture.Screen, Id, u8"How big it is"));
        Rectangle Box = TestUI_GetPixelBox(run, &Fixture, BoxId);
        TestUI_MovePointer(&Fixture, Box.x + 4.0f, Box.y + 4.0f);
        TestUI_Ticks(run, &Fixture, 3U);
        UITooltipState State;
        TEST_CHECK(run, UITooltip_GetCurrent(&Fixture.Screen, &State) && (State.WidgetId == Id) && (strcmp((const char*)State.Text, "How big it is") == 0));
        // Parts that do not show take no room; upright, the parts stand in a column.
        TEST_CHECK_SUCCESS(run, UIElement_SetScale(&Fixture.Screen, Id, 1.0f));
        TEST_CHECK_SUCCESS(run, UICompoundSlider_SetShows(&Fixture.Screen, Id, false, true, false));
        TestUI_Ticks(run, &Fixture, 2U);
        Rectangle Slider = TestUI_GetPixelBox(run, &Fixture, SliderId);
        TEST_CHECK(run, TestUI_IsSamePixel(Slider.x, 100.0f) && TestUI_IsSamePixel(Slider.width, 400.0f));
        TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, BoxId, &Part) && !Widget_IsRendered(Part));
        TEST_CHECK_SUCCESS(run, UICompoundSlider_SetShows(&Fixture.Screen, Id, true, true, true));
        TEST_CHECK_SUCCESS(run, UICompoundSlider_SetOrientation(&Fixture.Screen, Id, UISliderOrientation_Vertical));
        UISliderOrientation Orientation = UISliderOrientation_Horizontal;
        TEST_CHECK_SUCCESS(run, UISlider_GetOrientation(&Fixture.Screen, SliderId, &Orientation));
        TEST_CHECK(run, Orientation == UISliderOrientation_Vertical);
        TEST_CHECK_ERROR(run, UICompoundSlider_SetOrientation(&Fixture.Screen, Id, (UISliderOrientation)5), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_SUCCESS(run, UICompoundSlider_SetName(&Fixture.Screen, Id, u8"Height"));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void EscapeDuringAPartsDragIsTheCompoundsCancel(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t SliderId = WIDGET_ID_INVALID;
        uint64_t BoxId = WIDGET_ID_INVALID;
        uint64_t Id = AddCompound(run, &Fixture, &SliderId, &BoxId);
        Rectangle Slider = TestUI_GetPixelBox(run, &Fixture, SliderId);
        TestUI_MovePointer(&Fixture, Slider.x + 10.0f + (0.9f * (Slider.width - 20.0f)), Slider.y + 12.0f);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsAbout(Get(run, &Fixture, BoxId), 9.0));
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, (Get(run, &Fixture, BoxId) == 5.0) && (Get(run, &Fixture, Id) == 5.0));
        TEST_CHECK(run, CountEvents(&Events, Id, UIElementEvent_Cancelled) == 1U);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
    }
    TestUI_Deconstruct(run, &Fixture);
}


/* Settings set on a part by its own id are the compound's: both parts get them, and a value typed in the part is the compound's. */
static void SettingsSetOnAPartAreTheCompounds(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t SliderId = WIDGET_ID_INVALID;
        uint64_t BoxId = WIDGET_ID_INVALID;
        uint64_t Id = AddCompound(run, &Fixture, &SliderId, &BoxId);
        TEST_CHECK_SUCCESS(run, UIValue_SetRange(&Fixture.Screen, BoxId, true, 0.0, true, 100.0));
        UIValueSettings Settings;
        TEST_CHECK_SUCCESS(run, UIValue_GetSettings(&Fixture.Screen, Id, &Settings));
        TEST_CHECK(run, Settings.Maximum == 100.0);
        TEST_CHECK_SUCCESS(run, UIValue_GetSettings(&Fixture.Screen, SliderId, &Settings));
        TEST_CHECK(run, Settings.Maximum == 100.0);
        Rectangle Box = TestUI_GetPixelBox(run, &Fixture, BoxId);
        TestUI_Click(run, &Fixture, Box.x + (Box.width * 0.5f), Box.y + (Box.height * 0.5f));
        TypeText(run, &Fixture, "70");
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, (Get(run, &Fixture, Id) == 70.0) && (Get(run, &Fixture, SliderId) == 70.0) && (Get(run, &Fixture, BoxId) == 70.0));
        TEST_CHECK(run, (Events.Count > 0U) && (Events.Events[Events.Count - 1U].WidgetId == Id)
            && (Events.Events[Events.Count - 1U].Value.Data.DoubleValue == 70.0));
        // A setting that breaks the compound's rules (it needs both ends of a range) is refused through the part too.
        TEST_CHECK_ERROR(run, UIValue_SetRange(&Fixture.Screen, SliderId, false, 0.0, true, 100.0), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_SUCCESS(run, UIValue_GetSettings(&Fixture.Screen, BoxId, &Settings));
        TEST_CHECK(run, Settings.HasMinimum && (Settings.Maximum == 100.0));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Code that makes the compound mixed during a part's press is where the press now starts: Escape changes nothing, and every part agrees. */
static void APartsCancelAfterCodesMixedStateKeepsThePartsAgreed(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t SliderId = WIDGET_ID_INVALID;
        uint64_t BoxId = WIDGET_ID_INVALID;
        uint64_t Id = AddCompound(run, &Fixture, &SliderId, &BoxId);
        Rectangle Slider = TestUI_GetPixelBox(run, &Fixture, SliderId);
        TestUI_MovePointer(&Fixture, Slider.x + 10.0f + (0.5f * (Slider.width - 20.0f)), Slider.y + 12.0f); // On the thumb.
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIValue_SetMixed(&Fixture.Screen, Id, true));
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        uint64_t Ids[3] = { Id, SliderId, BoxId };
        bool IsEveryMixed = true;
        for (size_t Index = 0U; Index < 3U; Index++)
        {
            bool IsMixed = false;
            TEST_CHECK_SUCCESS(run, UIValue_IsMixed(&Fixture.Screen, Ids[Index], &IsMixed));
            IsEveryMixed = IsEveryMixed && IsMixed;
        }
        TEST_CHECK(run, IsEveryMixed && (CountValueEvents(&Events, Id) == 0U));
        // A drag after code's value: Escape goes back to code's value, in every part.
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIValue_Set(&Fixture.Screen, Id, 2.0));
        TestUI_MovePointer(&Fixture, Slider.x + 10.0f + (0.8f * (Slider.width - 20.0f)), Slider.y + 12.0f);
        TestUI_Tick(run, &Fixture);
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, (Get(run, &Fixture, Id) == 2.0) && (Get(run, &Fixture, SliderId) == 2.0) && (Get(run, &Fixture, BoxId) == 2.0));
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Tests: rows of fields.
static uint64_t AddRow(TestRun* run, TestUIFixture* fixture, size_t count, uint64_t* outFieldIds)
{
    UIVectorRowOptions Options = UIVectorRowOptions_Create();
    Options.Element = PlacedAt(100.0f, 100.0f, 400.0f, 24.0f);
    Options.Count = count;
    Options.Values[0] = 1.0;
    Options.Values[1] = 2.0;
    Options.Values[2] = 3.0;
    Options.Values[3] = 4.0;
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIKit_CreateVectorRow(&fixture->Kit, &fixture->Screen, WIDGET_ID_INVALID, &Options, &Id));
    for (size_t Axis = 0U; (Axis < count) && (outFieldIds != NULL); Axis++)
    {
        TEST_CHECK_SUCCESS(run, UIVectorRow_GetFieldId(&fixture->Screen, Id, Axis, &outFieldIds[Axis]));
    }
    TestUI_Ticks(run, fixture, 3U);
    return Id;
}

static void ARowsEventsSayWhichAxis(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Fields[3] = { WIDGET_ID_INVALID, WIDGET_ID_INVALID, WIDGET_ID_INVALID };
        uint64_t Id = AddRow(run, &Fixture, 3U, Fields);
        double Value = 0.0;
        TEST_CHECK_SUCCESS(run, UIVectorRow_GetValue(&Fixture.Screen, Id, 1U, &Value));
        TEST_CHECK(run, Value == 2.0);
        // The fields share the row evenly, each after its tag.
        Rectangle First = TestUI_GetPixelBox(run, &Fixture, Fields[0]);
        Rectangle Second = TestUI_GetPixelBox(run, &Fixture, Fields[1]);
        TEST_CHECK(run, TestUI_IsSamePixel(First.x, 116.0f) && (fabsf((Second.x - First.x) - ((400.0f + 4.0f) / 3.0f)) <= 1.0f)); // Whole pixels.
        // Typing in Y: one event per field, under the row's id, with the axis as its index.
        TestUI_Click(run, &Fixture, Second.x + (Second.width * 0.5f), Second.y + 12.0f);
        TypeText(run, &Fixture, "9");
        TestUI_Tap(run, &Fixture, GameKey_Tab); // Kept, and Z typed in at once.
        TypeText(run, &Fixture, "8");
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, (Events.Count == 4U) && (CountValueEvents(&Events, Fields[1]) == 0U));
        bool IsIndexed = (Events.Count == 4U);
        int32_t Axes[4] = { 1, 1, 2, 2 };
        double Values[4] = { 9.0, 9.0, 8.0, 8.0 };
        for (size_t Index = 0U; IsIndexed && (Index < 4U); Index++)
        {
            const UIEvent* Event = &Events.Events[Index];
            IsIndexed = (Event->WidgetId == Id) && Event->HasIndex && (Event->Index == Axes[Index]) && (Event->Value.Data.DoubleValue == Values[Index]);
        }
        TEST_CHECK(run, IsIndexed);
        // Mixed per axis.
        TEST_CHECK_SUCCESS(run, UIVectorRow_SetMixed(&Fixture.Screen, Id, 0U, true));
        bool IsMixed = false;
        TEST_CHECK_SUCCESS(run, UIValue_IsMixed(&Fixture.Screen, Fields[0], &IsMixed));
        TEST_CHECK(run, IsMixed);
        TEST_CHECK_SUCCESS(run, UIValue_IsMixed(&Fixture.Screen, Fields[1], &IsMixed));
        TEST_CHECK(run, !IsMixed);
        TEST_CHECK_SUCCESS(run, UIVectorRow_SetValue(&Fixture.Screen, Id, 2U, -4.5));
        TEST_CHECK(run, Get(run, &Fixture, Fields[2]) == -4.5);
        TEST_CHECK_ERROR(run, UIVectorRow_SetValue(&Fixture.Screen, Id, 3U, 1.0), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIVectorRow_GetFieldId(&Fixture.Screen, Fields[0], 0U, &Fields[0]), ErrorCode_InvalidOperation);
        Rectangle Tag = { 0 };
        Widget* Row = NULL;
        TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Row) && UIVectorRow_GetTagBox(Row, 0U, &Tag) && TestUI_IsSamePixel(Tag.width, 16.0f)
            && !UIVectorRow_GetTagBox(Row, 3U, &Tag));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ARowHasTwoToFourFields(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Fields[4] = { WIDGET_ID_INVALID, WIDGET_ID_INVALID, WIDGET_ID_INVALID, WIDGET_ID_INVALID };
        AddRow(run, &Fixture, 2U, Fields);
        AddRow(run, &Fixture, 4U, Fields);
        TEST_CHECK(run, Get(run, &Fixture, Fields[3]) == 4.0);
        UIVectorRowOptions Options = UIVectorRowOptions_Create();
        uint64_t Id = WIDGET_ID_INVALID;
        Options.Count = 1U;
        TEST_CHECK_ERROR(run, UIKit_CreateVectorRow(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Id), ErrorCode_ArgumentOutOfRange);
        Options.Count = 5U;
        TEST_CHECK_ERROR(run, UIKit_CreateVectorRow(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Id), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, Id == WIDGET_ID_INVALID);
    }
    TestUI_Deconstruct(run, &Fixture);
}

// Tests: the corners of forms, compounds and rows.
static Vector2 MeasureWidget(TestRun* run, TestUIFixture* fixture, uint64_t id, float availableWidth)
{
    Widget* Found = NULL;
    Vector2 Size = { .x = -1.0f, .y = -1.0f };
    if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&fixture->Screen, id, &Found)))
    {
        RenderContext Context;
        RenderContext_CreateMeasuring(&Context, (Vector2){ .x = TEST_UI_SURFACE_WIDTH, .y = TEST_UI_SURFACE_HEIGHT },
            TEST_UI_SURFACE_WIDTH / TEST_UI_SURFACE_HEIGHT, RenderTargetPosition_Centered());
        UIMeasureContext Sizing = { .Measure = &Context };
        TEST_CHECK_SUCCESS(run, Widget_Measure(Found, &Sizing, availableWidth, &Size));
    }
    return Size;
}

/* A form: rows its spacing apart inside its padding, measured so; only the rows across with no label width of their own and showing
 * share the label column; a row at its own scale counts its label at that scale; an element across is measured beside its label. */
static void AFormsRowsLineUpByTheirRules(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIFormOptions Options = UIFormOptions_Create();
        Options.Element = PlacedAt(100.0f, 50.0f, 400.0f, 300.0f);
        uint64_t FormId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateForm(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &FormId));
        uint64_t Toggles[5];
        uint64_t Rows[5];
        Rows[0] = AddLabelledToggle(run, &Fixture, FormId, u8"A", UILabelledLayout_Across, UIElementOptions_Create(), &Toggles[0]);
        Rows[1] = AddLabelledToggle(run, &Fixture, FormId, u8"Mid", UILabelledLayout_Across, UIElementOptions_Create(), &Toggles[1]);
        Rows[2] = AddLabelledToggle(run, &Fixture, FormId, u8"A stacked label", UILabelledLayout_Stacked, UIElementOptions_Create(), &Toggles[2]);
        Rows[3] = AddLabelledToggle(run, &Fixture, FormId, u8"Its own width here", UILabelledLayout_Across, UIElementOptions_Create(), &Toggles[3]);
        TEST_CHECK_SUCCESS(run, UILabelled_SetLabelWidth(&Fixture.Screen, Rows[3], true, 20.0f));
        Rows[4] = AddLabelledToggle(run, &Fixture, FormId, u8"A hidden longest label", UILabelledLayout_Across, UIElementOptions_Create(),
            &Toggles[4]);
        Widget* Hidden = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Rows[4], &Hidden)))
        {
            TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(Hidden, false));
        }
        TestUI_Ticks(run, &Fixture, 4U);
        // The column is the widest across row's that shares: "Mid", 3 glyphs of 8.
        TEST_CHECK(run, TestUI_IsSamePixel(TestUI_GetPixelBox(run, &Fixture, Toggles[0]).x, 105.0f + 24.0f + 10.0f));
        TEST_CHECK(run, TestUI_IsSamePixel(TestUI_GetPixelBox(run, &Fixture, Toggles[3]).x, 105.0f + 20.0f + 10.0f));
        // The spacing between the rows, the padding above the first; the form measured so.
        Rectangle First = TestUI_GetPixelBox(run, &Fixture, Rows[0]);
        Rectangle Second = TestUI_GetPixelBox(run, &Fixture, Rows[1]);
        Rectangle Third = TestUI_GetPixelBox(run, &Fixture, Rows[2]);
        Rectangle Fourth = TestUI_GetPixelBox(run, &Fixture, Rows[3]);
        TEST_CHECK(run, TestUI_IsSamePixel(First.y, 54.0f) && TestUI_IsSamePixel(Second.y, First.y + First.height + 6.0f)
            && TestUI_IsSamePixel(Third.y, Second.y + Second.height + 6.0f));
        Vector2 Size = MeasureWidget(run, &Fixture, FormId, 400.0f);
        TEST_CHECK(run, TestUI_IsSamePixel(Size.y, (Fourth.y + Fourth.height + 4.0f) - 50.0f));
        // A row at a scale of 2 counts its label at that scale: "Mid" there is 48 form pixels, the widest.
        TEST_CHECK_SUCCESS(run, UIElement_SetScale(&Fixture.Screen, Rows[1], 2.0f));
        TestUI_Ticks(run, &Fixture, 4U);
        TEST_CHECK(run, TestUI_IsSamePixel(TestUI_GetPixelBox(run, &Fixture, Toggles[0]).x, 105.0f + 48.0f + 10.0f));
        TEST_CHECK(run, TestUI_IsSamePixel(TestUI_GetPixelBox(run, &Fixture, Toggles[1]).x, 105.0f + 48.0f + 20.0f));
        // Measured with no bound, the scaled row is its label column (24 of its own) and its element, at 2.
        Vector2 Wide = MeasureWidget(run, &Fixture, Rows[1], UI_MEASURE_UNBOUNDED_WIDTH);
        Vector2 Unbounded = MeasureWidget(run, &Fixture, FormId, UI_MEASURE_UNBOUNDED_WIDTH);
        TEST_CHECK(run, TestUI_IsSamePixel(Unbounded.x, Wide.x + 10.0f));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* An element across is measured in the room beside its label: a wrapped label wraps there. */
static void AnElementAcrossIsMeasuredBesideItsLabel(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UILabelledOptions Options = UILabelledOptions_Create(u8"Notes");
        Options.Element = PlacedAt(100.0f, 100.0f, 200.0f, 100.0f);
        uint64_t Id = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateLabelled(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Id));
        UILabelOptions Text = UILabelOptions_Create(u8"aaaa bbbb cccc dddd eeee ffff gggg hhhh"); // Three lines beside the label, two in the whole row.
        Text.IsWrapped = true;
        uint64_t TextId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateLabel(&Fixture.Kit, &Fixture.Screen, Id, &Text, &TextId));
        TestUI_Ticks(run, &Fixture, 3U);
        Vector2 Beside = MeasureWidget(run, &Fixture, TextId, 200.0f - 40.0f - 10.0f);
        Vector2 Row = MeasureWidget(run, &Fixture, Id, 200.0f);
        TEST_CHECK(run, TestUI_IsSamePixel(Row.y, Beside.y) && (Beside.y > 16.0f));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A compound: its parts follow code's value during a part's drag, a stepped slow drag still moves, and a part's move ends the mixed state
 * in every part; upright, the box sits at the bottom; with no slider across, the box takes the room. */
static void ACompoundsPartsFollowAndLayOut(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UICompoundSliderOptions Options = UICompoundSliderOptions_Create();
        Options.Element = PlacedAt(100.0f, 100.0f, 400.0f, 24.0f);
        Options.Value = UIValueOptions_CreateRange(0.0, 10.0, 5.0);
        Options.Value.Settings.HasStep = true;
        Options.Value.Settings.Step = 1.0;
        Options.Value.IsMixed = true;
        Options.Name = u8"Size";
        uint64_t Id = WIDGET_ID_INVALID;
        uint64_t SliderId = WIDGET_ID_INVALID;
        uint64_t BoxId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateCompoundSlider(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Id));
        TEST_CHECK_SUCCESS(run, UICompoundSlider_GetPartIds(&Fixture.Screen, Id, NULL, &SliderId, &BoxId));
        TestUI_Ticks(run, &Fixture, 3U);
        Rectangle Slider = TestUI_GetPixelBox(run, &Fixture, SliderId);
        float X = Slider.x + 10.0f + (0.5f * (Slider.width - 20.0f));
        float Y = Slider.y + 12.0f;
        TestUI_MovePointer(&Fixture, X, Y);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        for (int Pixel = 1; Pixel <= 30; Pixel++) // A pixel at a time: the snapped value never holds the drag back.
        {
            TestUI_MovePointer(&Fixture, X + (float)Pixel, Y);
            TestUI_Tick(run, &Fixture);
        }
        TEST_CHECK(run, (Get(run, &Fixture, Id) == 6.0) && (Get(run, &Fixture, BoxId) == 6.0));
        bool IsMixed = true;
        TEST_CHECK_SUCCESS(run, UIValue_IsMixed(&Fixture.Screen, BoxId, &IsMixed));
        TEST_CHECK(run, !IsMixed);
        TEST_CHECK_SUCCESS(run, UIValue_Set(&Fixture.Screen, Id, 2.0)); // Code's value: the part's drag goes on from it.
        TestUI_MovePointer(&Fixture, X + 31.0f + 0.3f * (Slider.width - 20.0f), Y);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Get(run, &Fixture, SliderId) == 5.0);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);

        UICompoundSliderOptions Upright = UICompoundSliderOptions_Create();
        Upright.Element = PlacedAt(600.0f, 100.0f, 120.0f, 200.0f);
        Upright.Value = UIValueOptions_CreateRange(0.0, 10.0, 5.0);
        Upright.Orientation = UISliderOrientation_Vertical;
        Upright.Name = u8"Level";
        uint64_t UprightId = WIDGET_ID_INVALID;
        uint64_t UprightBox = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateCompoundSlider(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Upright, &UprightId));
        TEST_CHECK_SUCCESS(run, UICompoundSlider_GetPartIds(&Fixture.Screen, UprightId, NULL, NULL, &UprightBox));
        UICompoundSliderOptions Bare = UICompoundSliderOptions_Create();
        Bare.Element = PlacedAt(100.0f, 400.0f, 400.0f, 24.0f);
        Bare.Value = UIValueOptions_CreateRange(0.0, 10.0, 5.0);
        Bare.Name = u8"Size";
        Bare.ShowsSlider = false;
        uint64_t BareId = WIDGET_ID_INVALID;
        uint64_t BareBox = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateCompoundSlider(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Bare, &BareId));
        TEST_CHECK_SUCCESS(run, UICompoundSlider_GetPartIds(&Fixture.Screen, BareId, NULL, NULL, &BareBox));
        TestUI_Ticks(run, &Fixture, 3U);
        Rectangle Box = TestUI_GetPixelBox(run, &Fixture, UprightBox);
        TEST_CHECK(run, TestUI_IsSamePixel(Box.y + Box.height, 300.0f));
        Box = TestUI_GetPixelBox(run, &Fixture, BareBox);
        TEST_CHECK(run, TestUI_IsSamePixel(Box.x, 100.0f + 32.0f + 6.0f) && TestUI_IsSamePixel(Box.width, 400.0f - 38.0f));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A row's fields share its width evenly beside their tags, and it measures so; in a row too narrow for the tags, a tag never reaches past
 * its own room. */
static void ARowsFieldsShareItsWidthBesideTheirTags(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Fields[3] = { WIDGET_ID_INVALID, WIDGET_ID_INVALID, WIDGET_ID_INVALID };
        uint64_t Id = AddRow(run, &Fixture, 3U, Fields);
        float Each = (400.0f - 8.0f) / 3.0f;
        Rectangle First = TestUI_GetPixelBox(run, &Fixture, Fields[0]);
        Rectangle Second = TestUI_GetPixelBox(run, &Fixture, Fields[1]);
        // Boxes come in whole pixels: within one.
        TEST_CHECK(run, (fabsf(First.x - 116.0f) <= 1.0f) && (fabsf(First.width - (Each - 16.0f)) <= 1.0f));
        TEST_CHECK(run, fabsf(Second.x - (100.0f + Each + 4.0f + 16.0f)) <= 1.0f);
        Vector2 Field = MeasureWidget(run, &Fixture, Fields[0], UI_MEASURE_UNBOUNDED_WIDTH);
        Vector2 Size = MeasureWidget(run, &Fixture, Id, UI_MEASURE_UNBOUNDED_WIDTH);
        TEST_CHECK(run, fabsf(Size.x - ((3.0f * (16.0f + Field.x)) + 8.0f)) <= 0.01f);
        UIPlacement Narrow = UIPlacement_FromTopLeft(100.0f, 100.0f, 30.0f, 24.0f);
        Widget* Row = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Row)))
        {
            TEST_CHECK_SUCCESS(run, Widget_SetPlacement(Row, &Narrow));
            TestUI_Ticks(run, &Fixture, 3U);
            Rectangle Tag = { 0 };
            TEST_CHECK(run, UIVectorRow_GetTagBox(Row, 1U, &Tag));
            float Slot = (30.0f - 8.0f) / 3.0f;
            TEST_CHECK(run, (Tag.width <= (Slot + 0.01f)) && (Tag.x >= (Slot + 4.0f - 1.0f))); // In the second slot, not over the first field.
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}


void UICompoundTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "a_labelled_element_places_its_label_and_element", ALabelledElementPlacesItsLabelAndElement },
        { "a_forms_labels_line_up_on_the_widest", AFormsLabelsLineUpOnTheWidest },
        { "a_row_moved_out_of_its_form_uses_its_own_label_width", ARowMovedOutOfItsFormUsesItsOwnLabelWidth },
        { "a_compound_slider_keeps_its_parts_in_step", ACompoundSliderKeepsItsPartsInStep },
        { "a_compound_scales_as_one_and_lends_its_hover_text", ACompoundScalesAsOneAndLendsItsHoverText },
        { "escape_during_a_parts_drag_is_the_compounds_cancel", EscapeDuringAPartsDragIsTheCompoundsCancel },
        { "settings_set_on_a_part_are_the_compounds", SettingsSetOnAPartAreTheCompounds },
        { "a_parts_cancel_after_codes_mixed_state_keeps_the_parts_agreed", APartsCancelAfterCodesMixedStateKeepsThePartsAgreed },
        { "a_forms_rows_line_up_by_their_rules", AFormsRowsLineUpByTheirRules },
        { "an_element_across_is_measured_beside_its_label", AnElementAcrossIsMeasuredBesideItsLabel },
        { "a_compounds_parts_follow_and_lay_out", ACompoundsPartsFollowAndLayOut },
        { "a_rows_fields_share_its_width_beside_their_tags", ARowsFieldsShareItsWidthBesideTheirTags },
        { "a_rows_events_say_which_axis", ARowsEventsSayWhichAxis },
        { "a_row_has_two_to_four_fields", ARowHasTwoToFourFields },
    };
    TestRun_RunSuite(run, "UICompound", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
