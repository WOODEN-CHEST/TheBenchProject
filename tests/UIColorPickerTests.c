#include "TestSuites.h"
#include "TestUI.h"
#include "UIColorPicker.h"
#include "UIKit.h"
#include "UIPopup.h"
#include "UIWidgetFactory.h"
#include <string.h>


// Macros.
/** The fixture's look: a 40 x 20 swatch, a 100 pixel square, 10 pixel strips, 40 pixel fields. */
#define PICKER_THEME \
    "{\"name\": \"pickers\", \"font\": \"mono\", \"text_size\": 16, \"elements\": {" \
    "\"label\": {\"text\": {\"normal\": \"#e0e0e0\"}}," \
    "\"labelled\": {\"spacing\": 4}," \
    "\"scrollbar\": {\"thickness\": 10, \"min_thumb_length\": 10, \"thumb_inset\": 0, \"track\": {\"kind\": \"flat\"}, \"thumb\": {\"kind\": \"flat\"}}," \
    "\"text_box\": {\"padding\": [4, 2], \"min_size\": [60, 20], \"box\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#e0e0e0\"}}," \
    "\"property_box\": {\"padding\": [4, 2], \"min_size\": [30, 20], \"box\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#e0e0e0\"}}," \
    "\"color_picker\": {\"min_size\": [40, 20], \"checker_size\": 4, \"padding\": [5, 5], \"spacing\": 5, \"square_size\": 100, \"strip_width\": 10," \
    "\"field_width\": 40, \"swatch\": {\"kind\": \"flat\"}, \"checker_light\": {\"normal\": \"#ffffff\"}, \"checker_dark\": {\"normal\": \"#808080\"}," \
    "\"mixed\": {\"normal\": \"#e0e0e0\"}, \"popup\": {\"kind\": \"flat\"}, \"frame\": {\"kind\": \"none\"}, \"marker\": {\"kind\": \"flat\"}," \
    "\"strip_marker\": {\"kind\": \"flat\"}}}}"
#define SWATCH_X 100.0f
#define SWATCH_Y 50.0f


// Static functions: helpers.
static bool Construct(TestRun* run, TestUIFixture* fixture)
{
    return TestUI_Construct(run, fixture) && TestUI_SetTheme(run, fixture, PICKER_THEME);
}

static bool IsSameColor(Color a, Color b)
{
    return (a.r == b.r) && (a.g == b.g) && (a.b == b.b) && (a.a == b.a);
}

static uint64_t AddPicker(TestRun* run, TestUIFixture* fixture, const UIColorPickerOptions* options)
{
    UIColorPickerOptions Options = *options;
    Options.Element.HasPlacement = true;
    Options.Element.Placement = UIPlacement_FromTopLeft(SWATCH_X, SWATCH_Y, 40.0f, 20.0f);
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIKit_CreateColorPicker(&fixture->Kit, &fixture->Screen, WIDGET_ID_INVALID, &Options, &Id));
    TestUI_Ticks(run, fixture, 2U);
    return Id;
}

static UIColorPickerParts OpenAndGetParts(TestRun* run, TestUIFixture* fixture, uint64_t id)
{
    TestUI_Click(run, fixture, SWATCH_X + 20.0f, SWATCH_Y + 10.0f);
    TestUI_Ticks(run, fixture, 3U);
    UIColorPickerParts Parts;
    TEST_CHECK_SUCCESS(run, UIColorPicker_GetParts(&fixture->Screen, id, &Parts));
    return Parts;
}

static Color ReadPickerColor(TestRun* run, TestUIFixture* fixture, uint64_t id)
{
    Color Value = { 0 };
    TEST_CHECK_SUCCESS(run, UIColorPicker_GetColor(&fixture->Screen, id, &Value));
    return Value;
}

/* Presses on a part at a share of its box, drags to another and lets go there. */
static void DragOn(TestRun* run, TestUIFixture* fixture, uint64_t partId, Vector2 from, Vector2 to)
{
    Rectangle Box = TestUI_GetPixelBox(run, fixture, partId);
    TestUI_MovePointer(fixture, Box.x + (from.x * Box.width), Box.y + (from.y * Box.height));
    TestUI_Press(fixture, GameKey_MouseLeft);
    TestUI_Tick(run, fixture);
    TestUI_MovePointer(fixture, Box.x + (to.x * Box.width), Box.y + (to.y * Box.height));
    TestUI_Tick(run, fixture);
    TestUI_Release(fixture, GameKey_MouseLeft);
    TestUI_Tick(run, fixture);
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

static const UIEvent* FindLast(const TestUIEvents* events, uint64_t id, UIElementEvent event)
{
    const UIEvent* Found = NULL;
    for (size_t Index = 0U; Index < events->Count; Index++)
    {
        if ((events->Events[Index].WidgetId == id) && (events->Events[Index].WidgetEventId == (int32_t)event))
        {
            Found = &events->Events[Index];
        }
    }
    return Found;
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

static void SelectAll(TestRun* run, TestUIFixture* fixture)
{
    TestUI_Press(fixture, GameKey_LeftControl);
    TestUI_Tick(run, fixture);
    TestUI_Tap(run, fixture, GameKey_A);
    TestUI_Release(fixture, GameKey_LeftControl);
    TestUI_Tick(run, fixture);
}


// Tests: the conversions.
static void EveryColorComesBackThroughHsv(TestRun* run)
{
    size_t Wrong = 0U;
    for (int32_t Red = 0; Red < 256; Red++)
    {
        for (int32_t Green = 0; Green < 256; Green += 3)
        {
            for (int32_t Blue = 0; Blue < 256; Blue += 5)
            {
                Color Original = { .r = (unsigned char)Red, .g = (unsigned char)Green, .b = (unsigned char)Blue, .a = (unsigned char)(Blue ^ Red) };
                Color Back = UIColor_FromHsv(UIColor_ToHsv(Original), Original.a);
                Wrong += IsSameColor(Original, Back) ? 0U : 1U;
            }
        }
    }
    TEST_CHECK(run, Wrong == 0U);
    // Known values: the primaries, grey with no hue, and the hue's runs.
    UIColorHsv Hsv = UIColor_ToHsv((Color){ .r = 255U, .g = 0U, .b = 0U, .a = 255U });
    TEST_CHECK(run, (Hsv.Hue == 0.0) && (Hsv.Saturation == 1.0) && (Hsv.Value == 1.0));
    Hsv = UIColor_ToHsv((Color){ .r = 0U, .g = 255U, .b = 0U, .a = 255U });
    TEST_CHECK(run, Hsv.Hue == 120.0);
    Hsv = UIColor_ToHsv((Color){ .r = 255U, .g = 0U, .b = 255U, .a = 255U });
    TEST_CHECK(run, Hsv.Hue == 300.0);
    Hsv = UIColor_ToHsv((Color){ .r = 128U, .g = 128U, .b = 128U, .a = 255U });
    TEST_CHECK(run, (Hsv.Hue == 0.0) && (Hsv.Saturation == 0.0));
    double Red = 0.0;
    double Green = 0.0;
    double Blue = 0.0;
    UIColorHsv_ToRgb((UIColorHsv){ .Hue = 420.0, .Saturation = 2.0, .Value = 1.0 }, &Red, &Green, &Blue);
    TEST_CHECK(run, (Red == 1.0) && (Green == 1.0) && (Blue == 0.0)); // 420 wraps to 60: yellow; the saturation clamped.
    UIColorHsv_ToRgb((UIColorHsv){ .Hue = NAN, .Saturation = NAN, .Value = 0.5 }, &Red, &Green, &Blue);
    TEST_CHECK(run, (Red == 0.5) && (Green == 0.5) && (Blue == 0.5));
    UIColorHsv_ToRgb((UIColorHsv){ .Hue = -60.0, .Saturation = 1.0, .Value = 1.0 }, &Red, &Green, &Blue);
    TEST_CHECK(run, (Red == 1.0) && (Green == 0.0) && (Blue == 1.0)); // -60 is 300: magenta.
}

static void HexReadsAndWrites(TestRun* run)
{
    Color Read = { 0 };
    bool HasAlpha = true;
    TEST_CHECK(run, UIColor_TryParseHex(u8"#1a2B3c", 7U, &Read, &HasAlpha) && IsSameColor(Read, (Color){ 0x1aU, 0x2bU, 0x3cU, 255U }) && !HasAlpha);
    TEST_CHECK(run, UIColor_TryParseHex(u8"  ff000080 ", 11U, &Read, &HasAlpha) && IsSameColor(Read, (Color){ 255U, 0U, 0U, 0x80U }) && HasAlpha);
    TEST_CHECK(run, !UIColor_TryParseHex(u8"#12345", 6U, &Read, NULL));
    TEST_CHECK(run, !UIColor_TryParseHex(u8"#12345g", 7U, &Read, NULL));
    TEST_CHECK(run, !UIColor_TryParseHex(u8"##123456", 8U, &Read, NULL));
    TEST_CHECK(run, !UIColor_TryParseHex(u8"#1234567", 8U, &Read, NULL));
    TEST_CHECK(run, !UIColor_TryParseHex(u8"", 0U, &Read, NULL));
    TEST_CHECK(run, !UIColor_TryParseHex(u8"12 3456", 7U, &Read, NULL));
    unsigned char Text[UI_COLOR_HEX_BYTES];
    UIColor_WriteHex((Color){ 0x1aU, 0x2bU, 0x3cU, 0x4dU }, false, Text);
    TEST_CHECK(run, strcmp((const char*)Text, "#1A2B3C") == 0);
    UIColor_WriteHex((Color){ 0x1aU, 0x2bU, 0x3cU, 0x4dU }, true, Text);
    TEST_CHECK(run, strcmp((const char*)Text, "#1A2B3C4D") == 0);
    TEST_CHECK(run, UIColor_TryParseHex(Text, strlen((const char*)Text), &Read, &HasAlpha) && IsSameColor(Read, (Color){ 0x1aU, 0x2bU, 0x3cU, 0x4dU }));
}


// Tests: the element.
static void DraggingChangesAndClosingCommits(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UIColorPickerOptions Options = UIColorPickerOptions_Create();
        Options.Color = (Color){ 255U, 0U, 0U, 255U };
        uint64_t Id = AddPicker(run, &Fixture, &Options);
        UIColorPickerParts Parts = OpenAndGetParts(run, &Fixture, Id);
        TEST_CHECK(run, (Parts.PopupId != WIDGET_ID_INVALID) && UIScreen_IsPopupOpen(&Fixture.Screen, Parts.PopupId));
        TEST_CHECK(run, (Parts.FieldCount == 4U) && (Parts.AlphaId != WIDGET_ID_INVALID));
        // The hex field has the focus, entered, and shows the colour.
        const unsigned char* Text = NULL;
        size_t Length = 0U;
        TEST_CHECK_SUCCESS(run, UITextBox_GetText(&Fixture.Screen, Parts.HexId, &Text, &Length));
        TEST_CHECK(run, (Length == 9U) && (memcmp(Text, "#FF0000FF", 9U) == 0));
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.Screen) == Parts.HexId);
        // The square: to the left edge (no saturation), half way down: grey; Changed as it moves, no commit yet.
        Events.Count = 0U;
        DragOn(run, &Fixture, Parts.SquareId, (Vector2){ .x = 0.9f, .y = 0.1f }, (Vector2){ .x = 0.0f, .y = 0.5f });
        Color Grey = ReadPickerColor(run, &Fixture, Id);
        TEST_CHECK(run, (Grey.r == Grey.g) && (Grey.g == Grey.b) && (Grey.r >= 126U) && (Grey.r <= 129U) && (Grey.a == 255U));
        TEST_CHECK(run, (CountEvents(&Events, Id, UIElementEvent_Changed) >= 2U) && (CountEvents(&Events, Id, UIElementEvent_Committed) == 0U));
        const UIEvent* Changed = FindLast(&Events, Id, UIElementEvent_Changed);
        TEST_CHECK(run, (Changed != NULL) && (Changed->Value.Type == UIPropertyType_Color) && IsSameColor(Changed->Value.Data.ColorValue, Grey));
        // Back out of grey to the right: the hue was kept, so it is red again.
        DragOn(run, &Fixture, Parts.SquareId, (Vector2){ .x = 0.0f, .y = 0.0f }, (Vector2){ .x = 1.0f, .y = 0.0f });
        TEST_CHECK(run, IsSameColor(ReadPickerColor(run, &Fixture, Id), (Color){ 255U, 0U, 0U, 255U }));
        // The hue strip a third of the way: green; the alpha strip half way: about half alpha.
        DragOn(run, &Fixture, Parts.HueId, (Vector2){ .x = 0.5f, .y = 0.1f }, (Vector2){ .x = 0.5f, .y = 1.0f / 3.0f });
        Color Green = ReadPickerColor(run, &Fixture, Id);
        TEST_CHECK(run, (Green.g == 255U) && (Green.r <= 2U) && (Green.b <= 2U));
        DragOn(run, &Fixture, Parts.AlphaId, (Vector2){ .x = 0.5f, .y = 0.1f }, (Vector2){ .x = 0.5f, .y = 0.5f });
        TEST_CHECK(run, (ReadPickerColor(run, &Fixture, Id).a >= 126U) && (ReadPickerColor(run, &Fixture, Id).a <= 129U));
        // The fields follow (they are not being edited).
        double Alpha = 0.0;
        TEST_CHECK_SUCCESS(run, UIValue_Get(&Fixture.Screen, Parts.FieldIds[3], &Alpha));
        TEST_CHECK(run, Alpha == (double)ReadPickerColor(run, &Fixture, Id).a);
        // A press outside closes it, keeping the colour: one Committed.
        Events.Count = 0U;
        TestUI_Click(run, &Fixture, 900.0f, 450.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, !UIScreen_IsPopupOpen(&Fixture.Screen, Parts.PopupId) && (CountEvents(&Events, Id, UIElementEvent_Committed) == 1U));
        const UIEvent* Committed = FindLast(&Events, Id, UIElementEvent_Committed);
        TEST_CHECK(run, (Committed != NULL) && IsSameColor(Committed->Value.Data.ColorValue, ReadPickerColor(run, &Fixture, Id)));
        // Opened and closed with nothing changed: no commit.
        Events.Count = 0U;
        OpenAndGetParts(run, &Fixture, Id);
        TEST_CHECK_SUCCESS(run, UIColorPicker_Close(&Fixture.Screen, Id));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, CountEvents(&Events, Id, UIElementEvent_Committed) == 0U);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void EscapePutsTheColorBack(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UIColorPickerOptions Options = UIColorPickerOptions_Create();
        Options.Color = (Color){ 10U, 20U, 30U, 255U };
        uint64_t Id = AddPicker(run, &Fixture, &Options);
        UIColorPickerParts Parts = OpenAndGetParts(run, &Fixture, Id);
        DragOn(run, &Fixture, Parts.SquareId, (Vector2){ .x = 0.5f, .y = 0.5f }, (Vector2){ .x = 1.0f, .y = 0.0f });
        TEST_CHECK(run, !IsSameColor(ReadPickerColor(run, &Fixture, Id), Options.Color));
        // One Escape, the focus on the square the press gave it: the colour it opened with, Cancelled, closed; the game does not get it.
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == Parts.SquareId);
        Events.Count = 0U;
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsSameColor(ReadPickerColor(run, &Fixture, Id), Options.Color) && !UIScreen_IsPopupOpen(&Fixture.Screen, Parts.PopupId));
        TEST_CHECK(run, (CountEvents(&Events, Id, UIElementEvent_Cancelled) == 1U) && (CountEvents(&Events, Id, UIElementEvent_Committed) == 0U));
        TEST_CHECK(run, !GameInput_IsActionPressed(&Fixture.Input, GameAction_Pause));
        // The focus is back on the swatch; Enter opens it again.
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == Id);
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK_SUCCESS(run, UIColorPicker_GetParts(&Fixture.Screen, Id, &Parts));
        TEST_CHECK(run, UIScreen_IsPopupOpen(&Fixture.Screen, Parts.PopupId));
        // From inside the entered hex field too, in one press.
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.Screen) == Parts.HexId);
        SelectAll(run, &Fixture);
        TypeText(run, &Fixture, "ffffff");
        TEST_CHECK(run, IsSameColor(ReadPickerColor(run, &Fixture, Id), (Color){ 255U, 255U, 255U, 255U })); // Applied as it was typed.
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsSameColor(ReadPickerColor(run, &Fixture, Id), Options.Color) && !UIScreen_IsPopupOpen(&Fixture.Screen, Parts.PopupId));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void TheHexFieldTakesAColorAndRefusesTheRest(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UIColorPickerOptions Options = UIColorPickerOptions_Create();
        Options.Color = (Color){ 10U, 20U, 30U, 255U };
        uint64_t Id = AddPicker(run, &Fixture, &Options);
        UIColorPickerParts Parts = OpenAndGetParts(run, &Fixture, Id);
        // A bad hex: Enter keeps the field (it is put back) and closes; nothing changes.
        SelectAll(run, &Fixture);
        TypeText(run, &Fixture, "#12zz56");
        Events.Count = 0U;
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsSameColor(ReadPickerColor(run, &Fixture, Id), Options.Color) && (CountEvents(&Events, Id, UIElementEvent_Committed) == 0U));
        // A good one: Enter in one press applies it, closes and commits.
        Parts = OpenAndGetParts(run, &Fixture, Id);
        SelectAll(run, &Fixture);
        TypeText(run, &Fixture, "336699");
        Events.Count = 0U;
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsSameColor(ReadPickerColor(run, &Fixture, Id), (Color){ 0x33U, 0x66U, 0x99U, 255U }));
        TEST_CHECK(run, !UIScreen_IsPopupOpen(&Fixture.Screen, Parts.PopupId) && (CountEvents(&Events, Id, UIElementEvent_Committed) == 1U));
        // With its alpha.
        Parts = OpenAndGetParts(run, &Fixture, Id);
        SelectAll(run, &Fixture);
        TypeText(run, &Fixture, "#00000080");
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsSameColor(ReadPickerColor(run, &Fixture, Id), (Color){ 0U, 0U, 0U, 0x80U }));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void AChannelFieldMovesTheColor(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIColorPickerOptions Options = UIColorPickerOptions_Create();
        Options.Color = (Color){ 10U, 20U, 30U, 255U };
        uint64_t Id = AddPicker(run, &Fixture, &Options);
        UIColorPickerParts Parts = OpenAndGetParts(run, &Fixture, Id);
        // Tab from the hex field carries the editing round: the square (its arrows move the colour), the strips, then the red field.
        TestUI_Tap(run, &Fixture, GameKey_Tab);
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.Screen) == Parts.SquareId);
        Color Bright = { 200U, 100U, 50U, 255U };
        TEST_CHECK_SUCCESS(run, UIColorPicker_SetColor(&Fixture.Screen, Id, Bright));
        TestUI_Tap(run, &Fixture, GameKey_Left);
        Color Moved = ReadPickerColor(run, &Fixture, Id);
        TEST_CHECK(run, (Moved.r == 200U) && (Moved.b > Bright.b)); // Less saturated: a step toward grey.
        TestUI_Tap(run, &Fixture, GameKey_Down);
        TEST_CHECK(run, ReadPickerColor(run, &Fixture, Id).r < Moved.r); // Darker.
        TEST_CHECK_SUCCESS(run, UIColorPicker_SetColor(&Fixture.Screen, Id, Options.Color));
        TestUI_Tap(run, &Fixture, GameKey_Tab);
        TestUI_Tap(run, &Fixture, GameKey_Tab);
        TestUI_Tap(run, &Fixture, GameKey_Tab);
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.Screen) == Parts.FieldIds[0]);
        TypeText(run, &Fixture, "200");
        TestUI_Tap(run, &Fixture, GameKey_Tab);
        TEST_CHECK(run, ReadPickerColor(run, &Fixture, Id).r == 200U);
        TestUI_Tick(run, &Fixture);
        const unsigned char* Text = NULL;
        size_t Length = 0U;
        TEST_CHECK_SUCCESS(run, UITextBox_GetText(&Fixture.Screen, Parts.HexId, &Text, &Length));
        TEST_CHECK(run, (Length == 9U) && (memcmp(Text, "#C8141EFF", 9U) == 0));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void MixedShowsUntilAnEdit(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UIColorPickerOptions Options = UIColorPickerOptions_Create();
        Options.IsMixed = true;
        uint64_t Id = AddPicker(run, &Fixture, &Options);
        // Opened and closed untouched: still mixed, nothing posted.
        UIColorPickerParts Parts = OpenAndGetParts(run, &Fixture, Id);
        TEST_CHECK_SUCCESS(run, UIColorPicker_Close(&Fixture.Screen, Id));
        TestUI_Ticks(run, &Fixture, 2U);
        bool IsMixed = false;
        TEST_CHECK_SUCCESS(run, UIColorPicker_IsMixed(&Fixture.Screen, Id, &IsMixed));
        TEST_CHECK(run, IsMixed && (Events.Count == 0U));
        // An edit, even to the colour it holds, makes them one; Escape makes them mixed again.
        Parts = OpenAndGetParts(run, &Fixture, Id);
        DragOn(run, &Fixture, Parts.AlphaId, (Vector2){ .x = 0.5f, .y = 0.0f }, (Vector2){ .x = 0.5f, .y = 0.0f });
        TEST_CHECK_SUCCESS(run, UIColorPicker_IsMixed(&Fixture.Screen, Id, &IsMixed));
        TEST_CHECK(run, !IsMixed && (CountEvents(&Events, Id, UIElementEvent_Changed) == 1U));
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIColorPicker_IsMixed(&Fixture.Screen, Id, &IsMixed));
        TEST_CHECK(run, IsMixed && (CountEvents(&Events, Id, UIElementEvent_Cancelled) == 1U));
        // Kept: Committed though the colour is the same.
        Parts = OpenAndGetParts(run, &Fixture, Id);
        DragOn(run, &Fixture, Parts.AlphaId, (Vector2){ .x = 0.5f, .y = 0.0f }, (Vector2){ .x = 0.5f, .y = 0.0f });
        TEST_CHECK_SUCCESS(run, UIColorPicker_Close(&Fixture.Screen, Id));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, CountEvents(&Events, Id, UIElementEvent_Committed) == 1U);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ARenderColorKeepsItsBrightnessAndOpacity(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UIColorPickerOptions Options = UIColorPickerOptions_Create();
        Options.Kind = UIColorKind_RenderColor;
        Options.RenderColor = (RenderColor){ .Tint = { 200U, 100U, 50U, 17U }, .Brightness = 0.5f, .Opacity = 0.25f };
        uint64_t Id = AddPicker(run, &Fixture, &Options);
        UIColorPickerParts Parts = OpenAndGetParts(run, &Fixture, Id);
        TEST_CHECK(run, Parts.FieldCount == 5U); // Red, green, blue, brightness, opacity.
        double Brightness = 0.0;
        TEST_CHECK_SUCCESS(run, UIValue_Get(&Fixture.Screen, Parts.FieldIds[3], &Brightness));
        TEST_CHECK(run, Brightness == 0.5);
        DragOn(run, &Fixture, Parts.HueId, (Vector2){ .x = 0.5f, .y = 0.0f }, (Vector2){ .x = 0.5f, .y = 0.0f });
        RenderColor Value = { 0 };
        TEST_CHECK_SUCCESS(run, UIColorPicker_GetRenderColor(&Fixture.Screen, Id, &Value));
        TEST_CHECK(run, (Value.Brightness == 0.5f) && (Value.Opacity == 0.25f) && (Value.Tint.a == 17U) && (Value.Tint.r == 200U));
        const UIEvent* Changed = FindLast(&Events, Id, UIElementEvent_Changed);
        TEST_CHECK(run, (Changed != NULL) && (Changed->Value.Type == UIPropertyType_RenderColor));
        // As a Color: the opacity as the alpha.
        Color AsColor = ReadPickerColor(run, &Fixture, Id);
        TEST_CHECK(run, AsColor.a == 64U);
        // Without alpha a Color picker keeps its alpha and shows no strip for it.
        UIColorPickerOptions Plain = UIColorPickerOptions_Create();
        Plain.HasAlpha = false;
        Plain.Color = (Color){ 1U, 2U, 3U, 99U };
        uint64_t PlainId = WIDGET_ID_INVALID;
        Plain.Element.HasPlacement = true;
        Plain.Element.Placement = UIPlacement_FromTopLeft(500.0f, 50.0f, 40.0f, 20.0f);
        TEST_CHECK_SUCCESS(run, UIKit_CreateColorPicker(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Plain, &PlainId));
        TEST_CHECK_SUCCESS(run, UIColorPicker_Open(&Fixture.Screen, PlainId));
        TestUI_Ticks(run, &Fixture, 3U);
        TEST_CHECK_SUCCESS(run, UIColorPicker_GetParts(&Fixture.Screen, PlainId, &Parts));
        TEST_CHECK(run, (Parts.AlphaId == WIDGET_ID_INVALID) && (Parts.FieldCount == 3U));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ADisabledPickerAndStaleIds(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIColorPickerOptions Options = UIColorPickerOptions_Create();
        Options.IsEnabled = false;
        uint64_t Id = AddPicker(run, &Fixture, &Options);
        TestUI_Click(run, &Fixture, SWATCH_X + 20.0f, SWATCH_Y + 10.0f);
        TestUI_Tick(run, &Fixture);
        UIColorPickerParts Parts;
        TEST_CHECK_SUCCESS(run, UIColorPicker_GetParts(&Fixture.Screen, Id, &Parts));
        TEST_CHECK(run, (Parts.PopupId == WIDGET_ID_INVALID) && (UIScreen_GetFocusedWidgetId(&Fixture.Screen) != Id));
        TEST_CHECK_SUCCESS(run, UIColorPicker_SetIsEnabled(&Fixture.Screen, Id, true));
        Parts = OpenAndGetParts(run, &Fixture, Id);
        TEST_CHECK(run, Parts.PopupId != WIDGET_ID_INVALID);
        TEST_CHECK_SUCCESS(run, UIColorPicker_SetIsEnabled(&Fixture.Screen, Id, false));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, !UIScreen_IsPopupOpen(&Fixture.Screen, Parts.PopupId));
        // The swatch destroyed with its popup open: the popup goes too.
        TEST_CHECK_SUCCESS(run, UIColorPicker_SetIsEnabled(&Fixture.Screen, Id, true));
        Parts = OpenAndGetParts(run, &Fixture, Id);
        Widget* Swatch = NULL;
        TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Swatch) && TEST_CHECK_SUCCESS(run, Widget_Destroy(Swatch)));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, !UIScreen_IsWidgetAlive(&Fixture.Screen, Parts.PopupId));
        Color Unused = { 0 };
        TEST_CHECK_ERROR(run, UIColorPicker_GetColor(&Fixture.Screen, Id, &Unused), ErrorCode_InvalidOperation);
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Fields.
static const TestCase Cases[] =
{
    { "every_color_comes_back_through_hsv", EveryColorComesBackThroughHsv },
    { "hex_reads_and_writes", HexReadsAndWrites },
    { "dragging_changes_and_closing_commits", DraggingChangesAndClosingCommits },
    { "escape_puts_the_color_back", EscapePutsTheColorBack },
    { "the_hex_field_takes_a_color_and_refuses_the_rest", TheHexFieldTakesAColorAndRefusesTheRest },
    { "a_channel_field_moves_the_color", AChannelFieldMovesTheColor },
    { "mixed_shows_until_an_edit", MixedShowsUntilAnEdit },
    { "a_render_color_keeps_its_brightness_and_opacity", ARenderColorKeepsItsBrightnessAndOpacity },
    { "a_disabled_picker_and_stale_ids", ADisabledPickerAndStaleIds },
};


// Public functions.
void UIColorPickerTests_Run(TestRun* run)
{
    TestRun_RunSuite(run, "UIColorPicker", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
