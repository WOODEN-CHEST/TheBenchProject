#include "TestSuites.h"
#include "TestUI.h"
#include "UIContextMenu.h"
#include "UIKit.h"
#include "UIPopup.h"
#include "wr/WRMemory.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
#define BOX_THEME \
    "{\"name\": \"boxes\", \"font\": \"mono\", \"text_size\": 16, \"elements\": {" \
    "\"property_box\": {\"padding\": [6, 3], \"min_size\": [60, 22], \"box\": {\"kind\": \"flat\"}, \"fill\": {\"kind\": \"flat\"}," \
    "\"selection\": {\"kind\": \"flat\"}, \"caret\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#ffffff\"}, \"unit\": {\"normal\": \"#aaaaaa\"}}," \
    "\"menu\": {\"background\": {\"kind\": \"flat\"}, \"item\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#ffffff\"}, \"shortcut\": {\"normal\": \"#ffffff\"}, \"separator\": {\"kind\": \"flat\"}}}}"
/* A box at (100, 100), 200 x 24. */
#define BOX_X 100.0f
#define BOX_Y 100.0f
#define BOX_WIDTH 200.0f
#define BOX_HEIGHT 24.0f
#define MIDDLE_X (BOX_X + (BOX_WIDTH * 0.5f))
#define MIDDLE_Y (BOX_Y + (BOX_HEIGHT * 0.5f))


// Types.
typedef struct BoxClipboardStruct
{
    char Text[512];
} BoxClipboard;


// Static functions: helpers.
static Error Clipboard_GetText(void* self, GenericBuffer* outText, size_t maxBytes)
{
    BoxClipboard* Clipboard = self;
    size_t Length = strlen(Clipboard->Text);
    Length = (Length < maxBytes) ? Length : maxBytes;
    if ((Length > 0U) && !GenericBuffer_AppendRangeBytes(outText, (unsigned char*)Clipboard->Text, Length))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "test clipboard");
    }
    return Error_CreateSuccess();
}

static Error Clipboard_SetText(void* self, const unsigned char* text)
{
    BoxClipboard* Clipboard = self;
    snprintf(Clipboard->Text, sizeof(Clipboard->Text), "%s", (const char*)text);
    return Error_CreateSuccess();
}

static void Clipboard_Destroy(void* self)
{
    (void)self;
}

static const IClipboardVTable ClipboardVTable = { .GetText = Clipboard_GetText, .SetText = Clipboard_SetText, .Destroy = Clipboard_Destroy };

static bool Construct(TestRun* run, TestUIFixture* fixture, BoxClipboard* clipboard)
{
    if (!TestUI_Construct(run, fixture) || !TestUI_SetTheme(run, fixture, BOX_THEME))
    {
        return false;
    }
    Memory_Zero(clipboard, sizeof(*clipboard));
    GameInput_SetClipboard(&fixture->Input, (IClipboard){ .VTable = &ClipboardVTable, .Self = clipboard });
    return true;
}

static uint64_t AddBoxAt(TestRun* run, TestUIFixture* fixture, const UIPropertyBoxOptions* options, float x)
{
    UIPropertyBoxOptions Options = *options;
    Options.Element.HasPlacement = true;
    Options.Element.Placement = UIPlacement_FromTopLeft(x, BOX_Y, BOX_WIDTH, BOX_HEIGHT);
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIKit_CreatePropertyBox(&fixture->Kit, &fixture->Screen, WIDGET_ID_INVALID, &Options, &Id));
    TestUI_Tick(run, fixture);
    return Id;
}

static uint64_t AddBox(TestRun* run, TestUIFixture* fixture, const UIPropertyBoxOptions* options)
{
    return AddBoxAt(run, fixture, options, BOX_X);
}

static double Get(TestRun* run, TestUIFixture* fixture, uint64_t id)
{
    double Value = NAN;
    TEST_CHECK_SUCCESS(run, UIValue_Get(&fixture->Screen, id, &Value));
    return Value;
}

static UIPropertyBoxLookBase* GetBase(TestRun* run, TestUIFixture* fixture, uint64_t id)
{
    Widget* Found = NULL;
    return TEST_CHECK(run, UIScreen_TryGetWidgetById(&fixture->Screen, id, &Found)) ? (UIPropertyBoxLookBase*)(void*)Found : NULL;
}

static bool IsTyping(TestRun* run, TestUIFixture* fixture, uint64_t id)
{
    bool IsOn = false;
    TEST_CHECK_SUCCESS(run, UIPropertyBox_IsTyping(&fixture->Screen, id, &IsOn));
    return IsOn;
}

static bool IsAllSelected(TestRun* run, TestUIFixture* fixture, uint64_t id)
{
    UIPropertyBoxLookBase* Base = GetBase(run, fixture, id);
    if (Base == NULL)
    {
        return false;
    }
    size_t Start = 0U;
    size_t End = 0U;
    UITextEdit_GetSelection(&Base->TextBox.Edit, &Start, &End);
    return (Start == 0U) && (End == UITextEdit_GetLength(&Base->TextBox.Edit));
}

static bool IsShown(TestRun* run, TestUIFixture* fixture, uint64_t id, const char* text)
{
    UIPropertyBoxLookBase* Base = GetBase(run, fixture, id);
    return (Base != NULL) && (strcmp((const char*)UITextContent_Get(UIPropertyBoxCapability_GetShownText(&Base->PropertyBox)), text) == 0);
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

static void Hold(TestUIFixture* fixture, GameKey key, bool isDown)
{
    fixture->Sample.KeyStates[key] = isDown ? GAME_INPUT_KEY_STATE_DOWN : 0U;
}

static void MoveTo(TestRun* run, TestUIFixture* fixture, float x, float y)
{
    TestUI_MovePointer(fixture, x, y);
    TestUI_Tick(run, fixture);
}

/* A press at the box's middle, then a move past the threshold that starts the scrub, which changes nothing yet. */
static void BeginScrub(TestRun* run, TestUIFixture* fixture)
{
    TestUI_MovePointer(fixture, MIDDLE_X, MIDDLE_Y);
    TestUI_Press(fixture, GameKey_MouseLeft);
    TestUI_Tick(run, fixture);
    MoveTo(run, fixture, MIDDLE_X + 5.0f, MIDDLE_Y);
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

static const UIEvent* LastEvent(const TestUIEvents* events)
{
    return (events->Count > 0U) ? &events->Events[events->Count - 1U] : NULL;
}


// Tests: showing.
static void ShowingWritesItsDigitsAndUnitAndNeverChangesTheValue(TestRun* run)
{
    TestUIFixture Fixture;
    BoxClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        UIPropertyBoxOptions Options = UIPropertyBoxOptions_Create();
        Options.Value.Value = 1.23456789;
        Options.Unit = u8" m";
        uint64_t Id = AddBox(run, &Fixture, &Options);
        TestUI_Ticks(run, &Fixture, 3U);
        TEST_CHECK(run, IsShown(run, &Fixture, Id, "1.235"));
        UIPropertyBoxLookBase* Base = GetBase(run, &Fixture, Id);
        TEST_CHECK(run, (Base != NULL) && UIPropertyBoxCapability_ShowsUnit(&Base->PropertyBox));
        TEST_CHECK(run, Get(run, &Fixture, Id) == 1.23456789); // Shown at three digits, kept whole.
        TEST_CHECK_SUCCESS(run, UIPropertyBox_SetPrecision(&Fixture.Screen, Id, 1));
        TEST_CHECK(run, IsShown(run, &Fixture, Id, "1.2"));
        TEST_CHECK_SUCCESS(run, UIValue_SetIsInteger(&Fixture.Screen, Id, true));
        TEST_CHECK(run, IsShown(run, &Fixture, Id, "1"));
        TEST_CHECK_SUCCESS(run, UIValue_SetIsInteger(&Fixture.Screen, Id, false));
        TEST_CHECK_SUCCESS(run, UIValue_SetMixed(&Fixture.Screen, Id, true));
        TEST_CHECK(run, IsShown(run, &Fixture, Id, "-") && !UIPropertyBoxCapability_ShowsUnit(&GetBase(run, &Fixture, Id)->PropertyBox));
        TEST_CHECK_ERROR(run, UIPropertyBox_SetUnit(&Fixture.Screen, Id, u8"0123456789012345678901234567890123456789"), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIPropertyBox_SetUnit(&Fixture.Screen, Id, (const unsigned char*)"\xff"), ErrorCode_InvalidTextEncoding);
        // A fill only with a range, by the conversion, and not while mixed.
        TEST_CHECK(run, !UIPropertyBoxCapability_ShowsFill(&GetBase(run, &Fixture, Id)->PropertyBox));
        UIPropertyBoxOptions Ranged = UIPropertyBoxOptions_CreateRange(1.0, 100.0, 10.0);
        Ranged.Value.Settings.Conversion = UIValueConversion_Log;
        uint64_t Filled = AddBoxAt(run, &Fixture, &Ranged, 400.0f);
        UIPropertyBoxLookBase* FilledBase = GetBase(run, &Fixture, Filled);
        TEST_CHECK(run, (FilledBase != NULL) && UIPropertyBoxCapability_ShowsFill(&FilledBase->PropertyBox)
            && (fabs(UIPropertyBoxCapability_GetFillShare(&FilledBase->PropertyBox) - 0.5) < 1e-12));
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Tests: scrubbing.
static void APressBecomesAScrubOnlyPastTheThreshold(TestRun* run)
{
    TestUIFixture Fixture;
    BoxClipboard Clipboard;
    TestUIEvents Events;
    if (Construct(run, &Fixture, &Clipboard) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UIPropertyBoxOptions Options = UIPropertyBoxOptions_CreateRange(0.0, 100.0, 50.0);
        uint64_t Id = AddBox(run, &Fixture, &Options);
        TestUI_MovePointer(&Fixture, MIDDLE_X, MIDDLE_Y);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        MoveTo(run, &Fixture, MIDDLE_X + 2.0f, MIDDLE_Y + 9.0f); // Two pixels sideways and some down: under the threshold.
        TEST_CHECK(run, !UIScreen_IsPointerCaptured(&Fixture.Screen) && (Get(run, &Fixture, Id) == 50.0));
        MoveTo(run, &Fixture, MIDDLE_X + 4.0f, MIDDLE_Y);
        TEST_CHECK(run, UIScreen_IsPointerCaptured(&Fixture.Screen)); // Past it: a scrub, the cursor hidden ...
        TEST_CHECK(run, Get(run, &Fixture, Id) == 50.0); // ... and the value still where it was.
        Widget* Base = NULL;
        TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base) && !Base->_needsEntering && !Widget_IsEntered(Base));
        MoveTo(run, &Fixture, MIDDLE_X + 24.0f, MIDDLE_Y); // 20 pixels of a 200-pixel box: a tenth of the range.
        TEST_CHECK(run, fabs(Get(run, &Fixture, Id) - 60.0) < 1e-4);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, !UIScreen_IsPointerCaptured(&Fixture.Screen) && !IsTyping(run, &Fixture, Id)); // A scrub does not enter it.
        TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base) && Base->_needsEntering && !Widget_IsEntered(Base));
        TEST_CHECK(run, (CountEvents(&Events, Id, UIElementEvent_Committed) == 1U) && (CountEvents(&Events, Id, UIElementEvent_Changed) == 1U));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void AScrubWithoutARangeGoesByTheStepOrTheMagnitude(TestRun* run)
{
    TestUIFixture Fixture;
    BoxClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        UIPropertyBoxOptions Options = UIPropertyBoxOptions_Create();
        Options.Value.Value = 250.0;
        uint64_t Id = AddBox(run, &Fixture, &Options);
        BeginScrub(run, &Fixture);
        MoveTo(run, &Fixture, MIDDLE_X + 55.0f, MIDDLE_Y); // A hundredth of 100 per pixel: 50 pixels, 50 more.
        TEST_CHECK(run, fabs(Get(run, &Fixture, Id) - 300.0) < 1e-4); // (the pointer's place is a float)
        MoveTo(run, &Fixture, MIDDLE_X - 95.0f, MIDDLE_Y); // Turning back moves it at once.
        TEST_CHECK(run, fabs(Get(run, &Fixture, Id) - 150.0) < 1e-4);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);

        UIPropertyBoxOptions Stepped = UIPropertyBoxOptions_Create();
        Stepped.Value.Settings.HasStep = true;
        Stepped.Value.Settings.Step = 0.5;
        uint64_t SteppedId = AddBoxAt(run, &Fixture, &Stepped, 400.0f);
        TestUI_MovePointer(&Fixture, 500.0f, MIDDLE_Y);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        MoveTo(run, &Fixture, 505.0f, MIDDLE_Y);
        MoveTo(run, &Fixture, 509.0f, MIDDLE_Y); // A step every four pixels.
        TEST_CHECK(run, Get(run, &Fixture, SteppedId) == 0.5);
        MoveTo(run, &Fixture, 510.0f, MIDDLE_Y); // A pixel more is a quarter step: snapped, still built up.
        TEST_CHECK(run, Get(run, &Fixture, SteppedId) == 0.5);
        MoveTo(run, &Fixture, 512.0f, MIDDLE_Y);
        TEST_CHECK(run, Get(run, &Fixture, SteppedId) == 1.0);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ShiftSlowsAScrubAndCtrlSnapsIt(TestRun* run)
{
    TestUIFixture Fixture;
    BoxClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        UIPropertyBoxOptions Options = UIPropertyBoxOptions_CreateRange(0.0, 100.0, 50.0);
        uint64_t Id = AddBox(run, &Fixture, &Options);
        BeginScrub(run, &Fixture);
        Hold(&Fixture, GameKey_LeftShift, true);
        MoveTo(run, &Fixture, MIDDLE_X + 105.0f, MIDDLE_Y); // 100 pixels at a tenth: 5.
        TEST_CHECK(run, fabs(Get(run, &Fixture, Id) - 55.0) < 1e-4);
        Hold(&Fixture, GameKey_LeftShift, false);
        Hold(&Fixture, GameKey_LeftControl, true);
        MoveTo(run, &Fixture, MIDDLE_X + 111.0f, MIDDLE_Y); // 58 unsnapped, snapped to tens.
        TEST_CHECK(run, Get(run, &Fixture, Id) == 60.0);
        MoveTo(run, &Fixture, MIDDLE_X + 101.0f, MIDDLE_Y); // 53: back to 50, the place unsnapped below.
        TEST_CHECK(run, Get(run, &Fixture, Id) == 50.0);
        Hold(&Fixture, GameKey_LeftControl, false);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void SoftLimitsStopAScrubAndTypingGoesToTheHardOnes(TestRun* run)
{
    TestUIFixture Fixture;
    BoxClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        UIPropertyBoxOptions Options = UIPropertyBoxOptions_CreateRange(0.0, 1000.0, 5.0);
        Options.Value.Settings.HasSoftRange = true;
        Options.Value.Settings.SoftMinimum = 0.0;
        Options.Value.Settings.SoftMaximum = 10.0;
        uint64_t Id = AddBox(run, &Fixture, &Options);
        BeginScrub(run, &Fixture);
        MoveTo(run, &Fixture, 990.0f, MIDDLE_Y);
        TEST_CHECK(run, Get(run, &Fixture, Id) == 10.0); // A drag stops at the soft end.
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_Click(run, &Fixture, MIDDLE_X, MIDDLE_Y);
        TypeText(run, &Fixture, "500");
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, Get(run, &Fixture, Id) == 500.0); // Typing goes to the hard one.
        TestUI_Click(run, &Fixture, MIDDLE_X, MIDDLE_Y);
        TypeText(run, &Fixture, "5000");
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, Get(run, &Fixture, Id) == 1000.0); // Past it, clamped.
        // From past the soft range a scrub starts where the value is: its range widens to take it in.
        BeginScrub(run, &Fixture);
        MoveTo(run, &Fixture, MIDDLE_X - 15.0f, MIDDLE_Y);
        double Value = Get(run, &Fixture, Id);
        TEST_CHECK(run, (Value < 1000.0) && (Value > 800.0));
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void EscapeOrARightPressTakesAScrubBack(TestRun* run)
{
    TestUIFixture Fixture;
    BoxClipboard Clipboard;
    TestUIEvents Events;
    if (Construct(run, &Fixture, &Clipboard) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UIPropertyBoxOptions Options = UIPropertyBoxOptions_CreateRange(0.0, 100.0, 50.0);
        uint64_t Id = AddBox(run, &Fixture, &Options);
        BeginScrub(run, &Fixture);
        MoveTo(run, &Fixture, MIDDLE_X + 45.0f, MIDDLE_Y);
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, (Get(run, &Fixture, Id) == 50.0) && !UIScreen_IsPointerCaptured(&Fixture.Screen)); // Back, the cursor given back.
        TEST_CHECK(run, (LastEvent(&Events) != NULL) && (LastEvent(&Events)->WidgetEventId == (int32_t)UIElementEvent_Cancelled));
        Widget* Base = NULL;
        TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base) && Base->_needsEntering && Widget_IsFocused(Base)); // Escape went no further.
        MoveTo(run, &Fixture, MIDDLE_X + 80.0f, MIDDLE_Y); // What is left of the press does nothing.
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Get(run, &Fixture, Id) == 50.0) && !IsTyping(run, &Fixture, Id) && (CountEvents(&Events, Id, UIElementEvent_Committed) == 0U));

        // A right press takes it back too, and its release opens no menu.
        BeginScrub(run, &Fixture);
        MoveTo(run, &Fixture, MIDDLE_X + 45.0f, MIDDLE_Y);
        TestUI_Press(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Get(run, &Fixture, Id) == 50.0);
        TestUI_Release(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetPopupCount(&Fixture.Screen) == 0U);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        // The next right click does open it.
        TestUI_Press(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetPopupCount(&Fixture.Screen) == 1U);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ALostPressCommitsTheScrub(TestRun* run)
{
    TestUIFixture Fixture;
    BoxClipboard Clipboard;
    TestUIEvents Events;
    if (Construct(run, &Fixture, &Clipboard) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UIPropertyBoxOptions Options = UIPropertyBoxOptions_CreateRange(0.0, 100.0, 50.0);
        uint64_t Id = AddBox(run, &Fixture, &Options);
        BeginScrub(run, &Fixture);
        MoveTo(run, &Fixture, MIDDLE_X + 25.0f, MIDDLE_Y);
        Widget* Base = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base)))
        {
            TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(Base, false));
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, (CountEvents(&Events, Id, UIElementEvent_Committed) == 1U) && !UIScreen_IsPointerCaptured(&Fixture.Screen));
            TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base) && Base->_needsEntering);
        }
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Tests: typing.
static void AClickWithoutMovingTypesWithEverythingSelected(TestRun* run)
{
    TestUIFixture Fixture;
    BoxClipboard Clipboard;
    TestUIEvents Events;
    if (Construct(run, &Fixture, &Clipboard) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UIPropertyBoxOptions Options = UIPropertyBoxOptions_Create();
        Options.Value.Value = 1.25;
        uint64_t Id = AddBox(run, &Fixture, &Options);
        TestUI_Click(run, &Fixture, MIDDLE_X, MIDDLE_Y);
        TEST_CHECK(run, IsTyping(run, &Fixture, Id) && IsAllSelected(run, &Fixture, Id));
        const unsigned char* Text = NULL;
        TEST_CHECK_SUCCESS(run, UIPropertyBox_GetText(&Fixture.Screen, Id, &Text, NULL));
        TEST_CHECK(run, strcmp((const char*)Text, "1.25") == 0); // The shortest text that reads back.
        TypeText(run, &Fixture, "42");
        TEST_CHECK(run, (Get(run, &Fixture, Id) == 1.25) && (Events.Count == 0U)); // Nothing changes while typing.
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, (Get(run, &Fixture, Id) == 42.0) && !IsTyping(run, &Fixture, Id));
        TEST_CHECK(run, (CountEvents(&Events, Id, UIElementEvent_Changed) == 1U) && (CountEvents(&Events, Id, UIElementEvent_Committed) == 1U)
            && (LastEvent(&Events)->Value.Data.DoubleValue == 42.0));
        // Enter while focused enters it too, everything selected.
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, IsTyping(run, &Fixture, Id) && IsAllSelected(run, &Fixture, Id));
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, !IsTyping(run, &Fixture, Id));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ADoubleClickTypesWithEverythingSelected(TestRun* run)
{
    TestUIFixture Fixture;
    BoxClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        UIPropertyBoxOptions Options = UIPropertyBoxOptions_Create();
        Options.Value.Value = 12.5;
        uint64_t Id = AddBox(run, &Fixture, &Options);
        TestUI_Click(run, &Fixture, MIDDLE_X, MIDDLE_Y);
        TestUI_Click(run, &Fixture, MIDDLE_X, MIDDLE_Y);
        TEST_CHECK(run, IsTyping(run, &Fixture, Id) && IsAllSelected(run, &Fixture, Id)); // Not a word of it.
        TestUI_Click(run, &Fixture, BOX_X + 8.0f, MIDDLE_Y); // A later click places the caret, as in a text box.
        TEST_CHECK(run, IsTyping(run, &Fixture, Id) && !IsAllSelected(run, &Fixture, Id));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void BadTypedTextShowsInvalidAndGoesBack(TestRun* run)
{
    TestUIFixture Fixture;
    BoxClipboard Clipboard;
    TestUIEvents Events;
    if (Construct(run, &Fixture, &Clipboard) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UIPropertyBoxOptions Options = UIPropertyBoxOptions_CreateRange(0.0, 100.0, 7.0);
        Options.Unit = u8" m";
        uint64_t Id = AddBox(run, &Fixture, &Options);
        TestUI_Click(run, &Fixture, MIDDLE_X, MIDDLE_Y);
        TypeText(run, &Fixture, "abc");
        UIPropertyBoxLookBase* Base = GetBase(run, &Fixture, Id);
        TEST_CHECK(run, (Base != NULL) && (UIPropertyBoxCapability_GetState(Base) == UIElementState_Invalid));
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, (Get(run, &Fixture, Id) == 7.0) && (Events.Count == 0U) && IsShown(run, &Fixture, Id, "7.000"));
        Base = GetBase(run, &Fixture, Id);
        TEST_CHECK(run, (Base != NULL) && (UIPropertyBoxCapability_GetState(Base) != UIElementState_Invalid));
        // A unit typed after the number is passed over.
        TestUI_Click(run, &Fixture, MIDDLE_X, MIDDLE_Y);
        TypeText(run, &Fixture, "12 m");
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, Get(run, &Fixture, Id) == 12.0);
        // Escape while typing cancels: the value stays and nothing is posted.
        size_t Count = Events.Count;
        TestUI_Click(run, &Fixture, MIDDLE_X, MIDDLE_Y);
        TypeText(run, &Fixture, "99");
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, (Get(run, &Fixture, Id) == 12.0) && (Events.Count == Count) && !IsTyping(run, &Fixture, Id));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void TabCommitsAndTypesInTheNextBoxEitherWay(TestRun* run)
{
    TestUIFixture Fixture;
    BoxClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        UIPropertyBoxOptions Options = UIPropertyBoxOptions_Create();
        uint64_t First = AddBoxAt(run, &Fixture, &Options, 100.0f);
        uint64_t Second = AddBoxAt(run, &Fixture, &Options, 400.0f);
        TestUI_Click(run, &Fixture, MIDDLE_X, MIDDLE_Y);
        TypeText(run, &Fixture, "7");
        TestUI_Tap(run, &Fixture, GameKey_Tab);
        TEST_CHECK(run, (Get(run, &Fixture, First) == 7.0) && !IsTyping(run, &Fixture, First));
        TEST_CHECK(run, IsTyping(run, &Fixture, Second) && IsAllSelected(run, &Fixture, Second)); // Typing in the next at once.
        TypeText(run, &Fixture, "8");
        Hold(&Fixture, GameKey_LeftShift, true);
        TestUI_Tap(run, &Fixture, GameKey_Tab);
        Hold(&Fixture, GameKey_LeftShift, false);
        TEST_CHECK(run, (Get(run, &Fixture, Second) == 8.0) && IsTyping(run, &Fixture, First) && IsAllSelected(run, &Fixture, First));
        TestUI_Tap(run, &Fixture, GameKey_Escape);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void FarDoublesRoundTripThroughTheText(TestRun* run)
{
    TestUIFixture Fixture;
    BoxClipboard Clipboard;
    TestUIEvents Events;
    if (Construct(run, &Fixture, &Clipboard) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UIPropertyBoxOptions Options = UIPropertyBoxOptions_Create();
        Options.Value.Value = 1e7 + 0.1;
        uint64_t Id = AddBox(run, &Fixture, &Options);
        double Values[] = { 1e7 + 0.1, 5e6 + 0.123456789, -1234567.891011, 1.0 / 3.0, 6.02214076e23 };
        bool IsKept = true;
        for (size_t Index = 0U; Index < (sizeof(Values) / sizeof(Values[0])); Index++)
        {
            TEST_CHECK_SUCCESS(run, UIValue_Set(&Fixture.Screen, Id, Values[Index]));
            TestUI_Click(run, &Fixture, MIDDLE_X, MIDDLE_Y);
            TestUI_Tap(run, &Fixture, GameKey_Enter);
            double Value = Get(run, &Fixture, Id);
            IsKept = IsKept && (memcmp(&Value, &Values[Index], sizeof(double)) == 0);
        }
        TEST_CHECK(run, IsKept && (Events.Count == 0U));
        // Retyping the very text it showed changes nothing either.
        TEST_CHECK_SUCCESS(run, UIValue_Set(&Fixture.Screen, Id, 1e7 + 0.1));
        TestUI_Click(run, &Fixture, MIDDLE_X, MIDDLE_Y);
        TypeText(run, &Fixture, "10000000.1");
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, (Get(run, &Fixture, Id) == 1e7 + 0.1) && (Events.Count == 0U));
        // A float's value: shown as a float; a double that is no float is kept as it is when its text is left alone.
        TEST_CHECK_SUCCESS(run, UIValue_Set(&Fixture.Screen, Id, 0.1));
        UIValueSettings Settings;
        TEST_CHECK_SUCCESS(run, UIValue_GetSettings(&Fixture.Screen, Id, &Settings));
        Settings.IsSinglePrecision = true;
        UIScreen* Screen = &Fixture.Screen;
        Widget* Found = NULL;
        UIValueCapability* Piece = NULL;
        TEST_CHECK_SUCCESS(run, UIValue_Resolve(Screen, Id, &Found, &Piece));
        TEST_CHECK_SUCCESS(run, UIValueCapability_SetSettings(Piece, &Settings));
        TestUI_Click(run, &Fixture, MIDDLE_X, MIDDLE_Y);
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, Get(run, &Fixture, Id) == 0.1);
        TestUI_Click(run, &Fixture, MIDDLE_X, MIDDLE_Y);
        TypeText(run, &Fixture, "0.2");
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, Get(run, &Fixture, Id) == (double)0.2f); // Typed: read as a float.
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void MixedShowsADashUntilEdited(TestRun* run)
{
    TestUIFixture Fixture;
    BoxClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        UIPropertyBoxOptions Options = UIPropertyBoxOptions_CreateRange(0.0, 100.0, 40.0);
        Options.Value.IsMixed = true;
        uint64_t Id = AddBox(run, &Fixture, &Options);
        TEST_CHECK(run, IsShown(run, &Fixture, Id, "-"));
        TestUI_Click(run, &Fixture, MIDDLE_X, MIDDLE_Y); // Typed in and kept as it was, still mixed.
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        bool IsMixed = false;
        TEST_CHECK_SUCCESS(run, UIValue_IsMixed(&Fixture.Screen, Id, &IsMixed));
        TEST_CHECK(run, IsMixed);
        BeginScrub(run, &Fixture); // The first scrub starts from the caller's value.
        MoveTo(run, &Fixture, MIDDLE_X + 25.0f, MIDDLE_Y);
        TEST_CHECK(run, fabs(Get(run, &Fixture, Id) - 50.0) < 1e-4);
        TEST_CHECK_SUCCESS(run, UIValue_IsMixed(&Fixture.Screen, Id, &IsMixed));
        TEST_CHECK(run, !IsMixed);
        TestUI_Tap(run, &Fixture, GameKey_Escape); // Taken back: mixed again.
        TEST_CHECK_SUCCESS(run, UIValue_IsMixed(&Fixture.Screen, Id, &IsMixed));
        TEST_CHECK(run, IsMixed && IsShown(run, &Fixture, Id, "-"));
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_Click(run, &Fixture, MIDDLE_X, MIDDLE_Y);
        TypeText(run, &Fixture, "3");
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK_SUCCESS(run, UIValue_IsMixed(&Fixture.Screen, Id, &IsMixed));
        TEST_CHECK(run, !IsMixed && (Get(run, &Fixture, Id) == 3.0));
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Tests: strings, the wheel, resetting, the clipboard and the menu.
static void AStringTypesAtOnceAndPostsAtTheEnd(TestRun* run)
{
    TestUIFixture Fixture;
    BoxClipboard Clipboard;
    TestUIEvents Events;
    if (Construct(run, &Fixture, &Clipboard) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UIPropertyBoxOptions Options = UIPropertyBoxOptions_CreateString(u8"desk");
        Options.HasDefaultText = true;
        Options.DefaultText = u8"bench";
        uint64_t Id = AddBox(run, &Fixture, &Options);
        double Value = 0.0;
        TEST_CHECK_ERROR(run, UIValue_Get(&Fixture.Screen, Id, &Value), ErrorCode_InvalidOperation); // No value piece.
        TestUI_MovePointer(&Fixture, MIDDLE_X, MIDDLE_Y);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsTyping(run, &Fixture, Id) && IsAllSelected(run, &Fixture, Id)); // At the press.
        MoveTo(run, &Fixture, MIDDLE_X + 60.0f, MIDDLE_Y); // No scrub.
        TEST_CHECK(run, !UIScreen_IsPointerCaptured(&Fixture.Screen));
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TypeText(run, &Fixture, "lamp");
        TEST_CHECK(run, Events.Count == 0U);
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        const unsigned char* Text = NULL;
        size_t Length = 0U;
        TEST_CHECK_SUCCESS(run, UIPropertyBox_GetText(&Fixture.Screen, Id, &Text, &Length));
        TEST_CHECK(run, (strcmp((const char*)Text, "lamp") == 0) && (Length == 4U));
        TEST_CHECK(run, (Events.Count == 2U) && (LastEvent(&Events)->WidgetEventId == (int32_t)UIElementEvent_Committed)
            && (LastEvent(&Events)->Value.Data.DoubleValue == 4.0));
        // A middle click puts the default back as a whole change.
        TestUI_Press(&Fixture, GameKey_MouseMiddle);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseMiddle);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIPropertyBox_GetText(&Fixture.Screen, Id, &Text, NULL));
        TEST_CHECK(run, (strcmp((const char*)Text, "bench") == 0) && (Events.Count == 4U));
        TEST_CHECK_SUCCESS(run, UIPropertyBox_SetText(&Fixture.Screen, Id, u8"chair"));
        TEST_CHECK(run, IsShown(run, &Fixture, Id, "chair"));
        UITextEditLimits Limits = UITextEditLimits_Create();
        Limits.HasMaxCharacters = true;
        Limits.MaxCharacters = 3U;
        TEST_CHECK_SUCCESS(run, UIPropertyBox_SetTextLimits(&Fixture.Screen, Id, &Limits));
        TEST_CHECK_SUCCESS(run, UIPropertyBox_GetText(&Fixture.Screen, Id, &Text, NULL));
        TEST_CHECK(run, strcmp((const char*)Text, "cha") == 0);
        // A number's functions refuse a string and the string's a number.
        UIPropertyBoxOptions Number = UIPropertyBoxOptions_Create();
        uint64_t NumberId = AddBoxAt(run, &Fixture, &Number, 400.0f);
        TEST_CHECK_ERROR(run, UIPropertyBox_SetText(&Fixture.Screen, NumberId, u8"x"), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIPropertyBox_SetTextLimits(&Fixture.Screen, NumberId, &Limits), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIPropertyBox_SetDefaultText(&Fixture.Screen, NumberId, true, u8"x"), ErrorCode_InvalidOperation);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void OnlyCtrlWithTheWheelStepsANumber(TestRun* run)
{
    TestUIFixture Fixture;
    BoxClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        UIPropertyBoxOptions Options = UIPropertyBoxOptions_CreateRange(0.0, 10.0, 5.0);
        uint64_t Id = AddBox(run, &Fixture, &Options);
        TestUI_MovePointer(&Fixture, MIDDLE_X, MIDDLE_Y);
        TestUI_Tick(run, &Fixture);
        TestUI_Wheel(&Fixture, 1.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Get(run, &Fixture, Id) == 5.0);
        Hold(&Fixture, GameKey_LeftControl, true);
        TestUI_Wheel(&Fixture, 1.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, fabs(Get(run, &Fixture, Id) - 5.1) < 1e-9);
        Hold(&Fixture, GameKey_LeftControl, false);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void TheMiddleClickAndTheResetActionReset(TestRun* run)
{
    TestUIFixture Fixture;
    BoxClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        UIPropertyBoxOptions Options = UIPropertyBoxOptions_CreateRange(0.0, 10.0, 5.0);
        Options.Value.Settings.HasDefault = true;
        Options.Value.Settings.Default = 1.5;
        uint64_t Id = AddBox(run, &Fixture, &Options);
        TestUI_MovePointer(&Fixture, MIDDLE_X, MIDDLE_Y);
        TestUI_Press(&Fixture, GameKey_MouseMiddle);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseMiddle);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Get(run, &Fixture, Id) == 1.5);
        TEST_CHECK_SUCCESS(run, UIValue_Set(&Fixture.Screen, Id, 9.0));
        TestUI_Tap(run, &Fixture, GameKey_Backspace); // The reset action, only hovered.
        TEST_CHECK(run, Get(run, &Fixture, Id) == 1.5);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void HoverCopyAndPasteAndHostileClipboardText(TestRun* run)
{
    TestUIFixture Fixture;
    BoxClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        UIPropertyBoxOptions Options = UIPropertyBoxOptions_Create();
        Options.Value.Value = 12.5;
        Options.Unit = u8" m";
        uint64_t Id = AddBox(run, &Fixture, &Options);
        TestUI_MovePointer(&Fixture, MIDDLE_X, MIDDLE_Y);
        TestUI_Tick(run, &Fixture);
        Hold(&Fixture, GameKey_LeftControl, true);
        TestUI_Tap(run, &Fixture, GameKey_C);
        TEST_CHECK(run, strcmp(Clipboard.Text, "12.5") == 0); // No unit: it pastes anywhere.
        snprintf(Clipboard.Text, sizeof(Clipboard.Text), "7 m");
        TestUI_Tap(run, &Fixture, GameKey_V);
        TEST_CHECK(run, Get(run, &Fixture, Id) == 7.0);
        const char* Hostile[] = { "", "  ", "seven", "inf", "1e999", "7 kg", "\xc3\x28" };
        bool IsKept = true;
        for (size_t Index = 0U; Index < (sizeof(Hostile) / sizeof(Hostile[0])); Index++)
        {
            snprintf(Clipboard.Text, sizeof(Clipboard.Text), "%s", Hostile[Index]);
            TestUI_Tap(run, &Fixture, GameKey_V);
            IsKept = IsKept && (Get(run, &Fixture, Id) == 7.0);
        }
        memset(Clipboard.Text, '1', sizeof(Clipboard.Text) - 1U);
        Clipboard.Text[sizeof(Clipboard.Text) - 1U] = 0;
        TestUI_Tap(run, &Fixture, GameKey_V);
        TEST_CHECK(run, IsKept && (Get(run, &Fixture, Id) == 7.0));
        // A string pastes over its whole text.
        UIPropertyBoxOptions Word = UIPropertyBoxOptions_CreateString(u8"desk");
        uint64_t WordId = AddBoxAt(run, &Fixture, &Word, 400.0f);
        TestUI_MovePointer(&Fixture, 500.0f, MIDDLE_Y);
        TestUI_Tick(run, &Fixture);
        snprintf(Clipboard.Text, sizeof(Clipboard.Text), "lamp\r\npost");
        TestUI_Tap(run, &Fixture, GameKey_V);
        const unsigned char* Text = NULL;
        TEST_CHECK_SUCCESS(run, UIPropertyBox_GetText(&Fixture.Screen, WordId, &Text, NULL));
        TEST_CHECK(run, strcmp((const char*)Text, "lamp post") == 0); // One line: the line break became a space.
        Hold(&Fixture, GameKey_LeftControl, false);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ItsMenuIsResetCopyPasteOrWhileTypingATextBoxs(TestRun* run)
{
    TestUIFixture Fixture;
    BoxClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        UIPropertyBoxOptions Options = UIPropertyBoxOptions_CreateRange(0.0, 10.0, 5.0);
        uint64_t Id = AddBox(run, &Fixture, &Options);
        TestUI_MovePointer(&Fixture, MIDDLE_X, MIDDLE_Y);
        TestUI_Press(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        Widget* Menu = NULL;
        TEST_CHECK(run, (UIScreen_GetPopupCount(&Fixture.Screen) == 1U) && UIScreen_TryGetWidgetById(&Fixture.Screen, UIScreen_GetPopupAt(&Fixture.Screen, 0U), &Menu)
            && (Widget_GetSubWidgetCount(Menu) == 4U)); // Reset, a line, Copy, Paste.
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TestUI_Click(run, &Fixture, MIDDLE_X, MIDDLE_Y);
        TEST_CHECK(run, IsTyping(run, &Fixture, Id));
        TestUI_Press(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (UIScreen_GetPopupCount(&Fixture.Screen) == 1U) && UIScreen_TryGetWidgetById(&Fixture.Screen, UIScreen_GetPopupAt(&Fixture.Screen, 0U), &Menu)
            && (Widget_GetSubWidgetCount(Menu) == 9U)); // A text box's.
        TEST_CHECK(run, IsTyping(run, &Fixture, Id)); // The visit goes on through its menu.
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsTyping(run, &Fixture, Id));
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, !IsTyping(run, &Fixture, Id));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void DisabledItTakesAScrubBackAndNoFocus(TestRun* run)
{
    TestUIFixture Fixture;
    BoxClipboard Clipboard;
    TestUIEvents Events;
    if (Construct(run, &Fixture, &Clipboard) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UIPropertyBoxOptions Options = UIPropertyBoxOptions_CreateRange(0.0, 100.0, 50.0);
        uint64_t Id = AddBox(run, &Fixture, &Options);
        BeginScrub(run, &Fixture);
        MoveTo(run, &Fixture, MIDDLE_X + 45.0f, MIDDLE_Y);
        TEST_CHECK_SUCCESS(run, UIPropertyBox_SetIsEnabled(&Fixture.Screen, Id, false));
        TEST_CHECK(run, (Get(run, &Fixture, Id) == 50.0) && !UIScreen_IsPointerCaptured(&Fixture.Screen));
        Widget* Base = NULL;
        TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base) && !Widget_IsFocusable(Base) && Base->_needsEntering);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_Click(run, &Fixture, MIDDLE_X, MIDDLE_Y);
        TEST_CHECK(run, !IsTyping(run, &Fixture, Id));
        TEST_CHECK_SUCCESS(run, UIValue_SetIsEnabled(&Fixture.Screen, Id, true));
        TestUI_Click(run, &Fixture, MIDDLE_X, MIDDLE_Y);
        TEST_CHECK(run, IsTyping(run, &Fixture, Id));
    }
    TestUI_Deconstruct(run, &Fixture);
}

// Tests: what an independent review found.
static Vector2 MeasureBox(TestRun* run, TestUIFixture* fixture, uint64_t id)
{
    Widget* Found = NULL;
    Vector2 Size = { .x = -1.0f, .y = -1.0f };
    if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&fixture->Screen, id, &Found)))
    {
        RenderContext Context;
        RenderContext_CreateMeasuring(&Context, (Vector2){ .x = TEST_UI_SURFACE_WIDTH, .y = TEST_UI_SURFACE_HEIGHT },
            TEST_UI_SURFACE_WIDTH / TEST_UI_SURFACE_HEIGHT, RenderTargetPosition_Centered());
        UIMeasureContext Sizing = { .Measure = &Context };
        TEST_CHECK_SUCCESS(run, Widget_Measure(Found, &Sizing, UI_MEASURE_UNBOUNDED_WIDTH, &Size));
    }
    return Size;
}

static bool IsMixedNow(TestRun* run, TestUIFixture* fixture, uint64_t id)
{
    bool IsMixed = false;
    TEST_CHECK_SUCCESS(run, UIValue_IsMixed(&fixture->Screen, id, &IsMixed));
    return IsMixed;
}

/* Typing over everything selected with the value's own first characters keeps what is typed: 5 typed over as 5.5 is 5.5, not .5. */
static void TypingOverItsOwnTextKeepsEveryCharacter(TestRun* run)
{
    TestUIFixture Fixture;
    BoxClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        UIPropertyBoxOptions Options = UIPropertyBoxOptions_Create();
        Options.Value.Value = 5.0;
        uint64_t Id = AddBox(run, &Fixture, &Options);
        TestUI_Click(run, &Fixture, MIDDLE_X, MIDDLE_Y);
        TypeText(run, &Fixture, "5.5");
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, Get(run, &Fixture, Id) == 5.5);
        TEST_CHECK_SUCCESS(run, UIValue_Set(&Fixture.Screen, Id, 1.0));
        TestUI_Click(run, &Fixture, MIDDLE_X, MIDDLE_Y);
        TypeText(run, &Fixture, "10");
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, Get(run, &Fixture, Id) == 10.0);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Several values made one is a change though the number is the first value's: the reset action, a paste, typed text and a scrub alike. A
 * mixed box types from an empty text, so Enter on it changes nothing. */
static void MixedBecomesOneValueByAnyEdit(TestRun* run)
{
    TestUIFixture Fixture;
    BoxClipboard Clipboard;
    TestUIEvents Events;
    if (Construct(run, &Fixture, &Clipboard) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UIPropertyBoxOptions Options = UIPropertyBoxOptions_CreateRange(0.0, 10.0, 5.0);
        Options.Value.IsMixed = true;
        Options.Value.Settings.HasDefault = true;
        Options.Value.Settings.Default = 5.0;
        uint64_t Id = AddBox(run, &Fixture, &Options);
        TestUI_MovePointer(&Fixture, MIDDLE_X, MIDDLE_Y);
        TestUI_Tick(run, &Fixture);
        TestUI_Tap(run, &Fixture, GameKey_Backspace); // The reset action over it, to the number it holds.
        TEST_CHECK(run, !IsMixedNow(run, &Fixture, Id) && IsShown(run, &Fixture, Id, "5.000") && (CountEvents(&Events, Id, UIElementEvent_Committed) == 1U));
        TEST_CHECK_SUCCESS(run, UIValue_SetMixed(&Fixture.Screen, Id, true));
        snprintf(Clipboard.Text, sizeof(Clipboard.Text), "5");
        Hold(&Fixture, GameKey_LeftControl, true);
        TestUI_Tap(run, &Fixture, GameKey_V); // Its own number pasted.
        Hold(&Fixture, GameKey_LeftControl, false);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, !IsMixedNow(run, &Fixture, Id) && (CountEvents(&Events, Id, UIElementEvent_Committed) == 2U));
        TEST_CHECK_SUCCESS(run, UIValue_SetMixed(&Fixture.Screen, Id, true));
        TestUI_Click(run, &Fixture, MIDDLE_X, MIDDLE_Y);
        UIPropertyBoxLookBase* Base = GetBase(run, &Fixture, Id);
        TEST_CHECK(run, IsTyping(run, &Fixture, Id) && (Base != NULL) && (UITextEdit_GetLength(&Base->TextBox.Edit) == 0U));
        TestUI_Tap(run, &Fixture, GameKey_Enter); // The empty text kept: still several values.
        TEST_CHECK(run, IsMixedNow(run, &Fixture, Id) && (CountEvents(&Events, Id, UIElementEvent_Committed) == 2U));
        TestUI_Click(run, &Fixture, MIDDLE_X, MIDDLE_Y);
        TypeText(run, &Fixture, "05");
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, !IsMixedNow(run, &Fixture, Id) && (Get(run, &Fixture, Id) == 5.0) && (CountEvents(&Events, Id, UIElementEvent_Committed) == 3U));
        TEST_CHECK_SUCCESS(run, UIValue_SetMixed(&Fixture.Screen, Id, true));
        BeginScrub(run, &Fixture); // Away and back to where it began: one value, committed.
        MoveTo(run, &Fixture, MIDDLE_X + 45.0f, MIDDLE_Y);
        MoveTo(run, &Fixture, MIDDLE_X + 5.0f, MIDDLE_Y);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, !IsMixedNow(run, &Fixture, Id) && (CountEvents(&Events, Id, UIElementEvent_Committed) == 4U));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* The menu's Paste reads a number ending in the box's unit, as Ctrl+V does. */
static void ItsMenusPasteTakesItsUnit(TestRun* run)
{
    TestUIFixture Fixture;
    BoxClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        UIPropertyBoxOptions Options = UIPropertyBoxOptions_Create();
        Options.Value.Value = 1.0;
        Options.Unit = u8" m";
        uint64_t Id = AddBox(run, &Fixture, &Options);
        snprintf(Clipboard.Text, sizeof(Clipboard.Text), "7 m");
        TestUI_MovePointer(&Fixture, MIDDLE_X, MIDDLE_Y);
        TestUI_Press(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseRight);
        TestUI_Ticks(run, &Fixture, 3U); // The menu laid out.
        Widget* Menu = NULL;
        if (TEST_CHECK(run, (UIScreen_GetPopupCount(&Fixture.Screen) == 1U)
            && UIScreen_TryGetWidgetById(&Fixture.Screen, UIScreen_GetPopupAt(&Fixture.Screen, 0U), &Menu) && (Widget_GetSubWidgetCount(Menu) == 4U)))
        {
            Rectangle Paste = TestUI_GetPixelBox(run, &Fixture, Widget_GetId(Widget_GetSubWidgetAt(Menu, 3U)));
            TestUI_Click(run, &Fixture, Paste.x + 4.0f, Paste.y + (Paste.height * 0.5f));
            TestUI_Tick(run, &Fixture);
        }
        TEST_CHECK(run, Get(run, &Fixture, Id) == 7.0);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* What it shows is measured again when it changes, so a box sized by its content grows with its number. */
static void ItsSizeFollowsWhatItShows(TestRun* run)
{
    TestUIFixture Fixture;
    BoxClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        UIPropertyBoxOptions Options = UIPropertyBoxOptions_Create();
        Options.Value.Value = 5.0;
        uint64_t Id = AddBox(run, &Fixture, &Options);
        TestUI_Ticks(run, &Fixture, 2U);
        Vector2 Small = MeasureBox(run, &Fixture, Id);
        TEST_CHECK_SUCCESS(run, UIValue_Set(&Fixture.Screen, Id, 123456789.0));
        TestUI_Ticks(run, &Fixture, 2U);
        Vector2 Large = MeasureBox(run, &Fixture, Id);
        TEST_CHECK(run, Large.x > Small.x);
        TEST_CHECK_SUCCESS(run, UIPropertyBox_SetPrecision(&Fixture.Screen, Id, 0));
        TestUI_Ticks(run, &Fixture, 2U);
        Vector2 Fewer = MeasureBox(run, &Fixture, Id);
        TEST_CHECK(run, (Fewer.x < Large.x) && (Fewer.x > Small.x));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A right press during a left one takes the press back and opens no menu: no scrub runs under a menu, and the release types nothing. A
 * right press let go off the box leaves nothing behind that swallows the menu key's request. */
static void ARightPressDuringAPressTakesItBack(TestRun* run)
{
    TestUIFixture Fixture;
    BoxClipboard Clipboard;
    TestUIEvents Events;
    if (Construct(run, &Fixture, &Clipboard) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UIPropertyBoxOptions Options = UIPropertyBoxOptions_CreateRange(0.0, 100.0, 50.0);
        uint64_t Id = AddBox(run, &Fixture, &Options);
        TestUI_MovePointer(&Fixture, MIDDLE_X, MIDDLE_Y);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_Press(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetPopupCount(&Fixture.Screen) == 0U);
        MoveTo(run, &Fixture, MIDDLE_X + 5.0f, MIDDLE_Y);
        MoveTo(run, &Fixture, MIDDLE_X + 45.0f, MIDDLE_Y);
        TEST_CHECK(run, (Get(run, &Fixture, Id) == 50.0) && !UIScreen_IsPointerCaptured(&Fixture.Screen));
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, !IsTyping(run, &Fixture, Id) && (Events.Count == 0U || CountEvents(&Events, Id, UIElementEvent_Committed) == 0U));

        // A scrub taken back by a right press let go off the box: the menu key still opens the menu.
        TestUI_MovePointer(&Fixture, MIDDLE_X, MIDDLE_Y);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        MoveTo(run, &Fixture, MIDDLE_X + 8.0f, MIDDLE_Y);
        TestUI_Press(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        MoveTo(run, &Fixture, 900.0f, 450.0f);
        TestUI_Release(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetPopupCount(&Fixture.Screen) == 0U);
        Widget* Box = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Box)))
        {
            TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, Box));
        }
        TEST_CHECK_SUCCESS(run, UIContextMenu_Request(&Fixture.Screen, Id, (Vector2){ .x = 0.2f, .y = 0.2f }, true, NULL));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetPopupCount(&Fixture.Screen) == 1U);
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Tests: scrubbing's and typing's corners, and what is not the box's.
static Error RecordBoxCue(UIScreen* screen, const UIEvent* event, const UserData* userData)
{
    (void)screen;
    size_t* Count = UserData_GetPointer(userData);
    *Count += (event->WidgetEventId == (int32_t)UIElementCue_Refused) ? 1U : 0U;
    return Error_CreateSuccess();
}

/* A scrub's place keeps to the range and to the hard limits (turning back moves the value at once); its rate and its coarse snap count by
 * the value it began at; code's value during it is where it goes on from. */
static void AScrubsPlaceKeepsToItsLimitsAndItsStart(TestRun* run)
{
    TestUIFixture Fixture;
    BoxClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        UIPropertyBoxOptions Options = UIPropertyBoxOptions_CreateRange(0.0, 100.0, 90.0);
        uint64_t Id = AddBox(run, &Fixture, &Options);
        BeginScrub(run, &Fixture);
        MoveTo(run, &Fixture, MIDDLE_X + 45.0f, MIDDLE_Y); // 40 pixels of 200: past 100, held there ...
        TEST_CHECK(run, Get(run, &Fixture, Id) == 100.0);
        MoveTo(run, &Fixture, MIDDLE_X + 41.0f, MIDDLE_Y); // ... and turning back moves it at once.
        TEST_CHECK(run, fabs(Get(run, &Fixture, Id) - 98.0) < 1e-4);
        TEST_CHECK_SUCCESS(run, UIValue_Set(&Fixture.Screen, Id, 20.0)); // Code's value: the scrub goes on from it.
        MoveTo(run, &Fixture, MIDDLE_X + 61.0f, MIDDLE_Y);
        TEST_CHECK(run, fabs(Get(run, &Fixture, Id) - 30.0) < 1e-4);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);

        // No range (only a maximum): the place keeps to it, the rate is the start's magnitude's (5: a hundredth per pixel).
        UIPropertyBoxOptions Open = UIPropertyBoxOptions_Create();
        Open.Value.Value = 9.5;
        Open.Value.Settings.HasMaximum = true;
        Open.Value.Settings.Maximum = 10.0;
        uint64_t OpenId = AddBoxAt(run, &Fixture, &Open, 400.0f);
        float X = 500.0f;
        TestUI_MovePointer(&Fixture, X, MIDDLE_Y);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        MoveTo(run, &Fixture, X + 5.0f, MIDDLE_Y);
        MoveTo(run, &Fixture, X + 105.0f, MIDDLE_Y);
        TEST_CHECK(run, Get(run, &Fixture, OpenId) == 10.0);
        MoveTo(run, &Fixture, X + 95.0f, MIDDLE_Y);
        TEST_CHECK(run, fabs(Get(run, &Fixture, OpenId) - 9.9) < 1e-5); // The pointer's place is a float: its noise.
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        UIPropertyBoxOptions Free = UIPropertyBoxOptions_Create();
        Free.Value.Value = 5.0;
        uint64_t FreeId = AddBoxAt(run, &Fixture, &Free, 700.0f);
        X = 800.0f;
        TestUI_MovePointer(&Fixture, X, MIDDLE_Y);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        MoveTo(run, &Fixture, X + 5.0f, MIDDLE_Y);
        MoveTo(run, &Fixture, X + 1005.0f, MIDDLE_Y); // Past 10 the rate stays the start's.
        TEST_CHECK(run, fabs(Get(run, &Fixture, FreeId) - 15.0) < 1e-5);
        Hold(&Fixture, GameKey_LeftControl, true); // Coarse: tenths, the start's magnitude's.
        MoveTo(run, &Fixture, X + 1042.0f, MIDDLE_Y);
        TEST_CHECK(run, fabs(Get(run, &Fixture, FreeId) - 15.4) < 1e-9);
        Hold(&Fixture, GameKey_LeftControl, false);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* At a scale of 2 the threshold and the scrub count UI pixels, not the screen's. */
static void AScrubCountsUIPixels(TestRun* run)
{
    TestUIFixture Fixture;
    BoxClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        TEST_CHECK_SUCCESS(run, UIScreen_SetScale(&Fixture.Screen, 2.0f));
        UIPropertyBoxOptions Options = UIPropertyBoxOptions_CreateRange(0.0, 100.0, 50.0);
        uint64_t Id = AddBox(run, &Fixture, &Options);
        TestUI_Tick(run, &Fixture);
        Rectangle Box = TestUI_GetPixelBox(run, &Fixture, Id);
        float X = Box.x + (Box.width * 0.5f);
        float Y = Box.y + (Box.height * 0.5f);
        TestUI_MovePointer(&Fixture, X, Y);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        MoveTo(run, &Fixture, X + 4.0f, Y); // 2 UI pixels: not yet.
        TEST_CHECK(run, !GetBase(run, &Fixture, Id)->PropertyBox._isScrubbing);
        MoveTo(run, &Fixture, X + 8.0f, Y); // 4: it scrubs from here.
        MoveTo(run, &Fixture, X + 48.0f, Y); // 20 UI pixels of 200: a tenth of the range.
        TEST_CHECK(run, fabs(Get(run, &Fixture, Id) - 60.0) < 1e-4);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Typing: at most UI_VALUE_MAX_TEXT_BYTES, no overwrite (Insert inserts), a visit through its own menu goes on as it was (and shows as
 * entered meanwhile), bad text left posts the Refused cue, and a number not typed in has no text to read. */
static void TypingInANumberKeepsItsRules(TestRun* run)
{
    TestUIFixture Fixture;
    BoxClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        UIPropertyBoxOptions Options = UIPropertyBoxOptions_Create();
        Options.Value.Value = 12.0;
        uint64_t Id = AddBox(run, &Fixture, &Options);
        size_t Refused = 0U;
        UserData Data = UserData_FromPointer(&Refused);
        uint64_t Subscription = 0U;
        TEST_CHECK_SUCCESS(run, UIScreen_Subscribe(&Fixture.Screen, Id, UI_EVENT_KIND_MASK(UIEventKind_Cue), RecordBoxCue, &Data, &Subscription));
        TestUI_Click(run, &Fixture, MIDDLE_X, MIDDLE_Y);
        char Digits[UI_VALUE_MAX_TEXT_BYTES + 3U];
        memset(Digits, '1', sizeof(Digits) - 1U);
        Digits[sizeof(Digits) - 1U] = 0;
        TypeText(run, &Fixture, Digits);
        UIPropertyBoxLookBase* Base = GetBase(run, &Fixture, Id);
        TEST_CHECK(run, (Base != NULL) && (UITextEdit_GetLength(&Base->TextBox.Edit) == UI_VALUE_MAX_TEXT_BYTES));
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TestUI_Click(run, &Fixture, MIDDLE_X, MIDDLE_Y);
        TestUI_Tap(run, &Fixture, GameKey_Home);
        TestUI_Tap(run, &Fixture, GameKey_Insert);
        TypeText(run, &Fixture, "9");
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, Get(run, &Fixture, Id) == 912.0);
        const unsigned char* Text = NULL;
        size_t Length = 99U;
        TEST_CHECK_SUCCESS(run, UIPropertyBox_GetText(&Fixture.Screen, Id, &Text, &Length));
        TEST_CHECK(run, Length == 0U);
        // Through its own menu: typed 7, the menu opened and closed, then 5: 75; entered all the while.
        TestUI_Click(run, &Fixture, MIDDLE_X, MIDDLE_Y);
        TypeText(run, &Fixture, "7");
        TestUI_Press(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseRight);
        TestUI_Ticks(run, &Fixture, 2U);
        Base = GetBase(run, &Fixture, Id);
        TEST_CHECK(run, (UIScreen_GetPopupCount(&Fixture.Screen) == 1U) && (Base != NULL)
            && (UIPropertyBoxCapability_GetState(Base) == UIElementState_Entered));
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TestUI_Tick(run, &Fixture);
        TypeText(run, &Fixture, "5");
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, Get(run, &Fixture, Id) == 75.0);
        // Text that is no number, left with Enter: the cue says it was refused; a paste of it too.
        TestUI_Click(run, &Fixture, MIDDLE_X, MIDDLE_Y);
        TypeText(run, &Fixture, "x");
        size_t Before = Refused;
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, (Refused == (Before + 1U)) && (Get(run, &Fixture, Id) == 75.0));
        snprintf(Clipboard.Text, sizeof(Clipboard.Text), "seven");
        Hold(&Fixture, GameKey_LeftControl, true);
        TestUI_Tap(run, &Fixture, GameKey_V);
        Hold(&Fixture, GameKey_LeftControl, false);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Refused == (Before + 2U)) && (Get(run, &Fixture, Id) == 75.0));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Keys and presses that are not the box's: the reset action with no default and the menu key delivered by hover pass through; Ctrl and the
 * wheel do nothing during a press; a middle press let go off the box or during a left press resets nothing; a string's menu chosen after
 * a disable does nothing. A long or broken unit is refused; a mixed or string box shows no fill. */
static void WhatIsNotTheBoxsPassesThrough(TestRun* run)
{
    TestUIFixture Fixture;
    BoxClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        UIPropertyBoxOptions Options = UIPropertyBoxOptions_CreateRange(0.0, 100.0, 50.0);
        uint64_t Id = AddBox(run, &Fixture, &Options);
        TestUI_MovePointer(&Fixture, MIDDLE_X, MIDDLE_Y);
        TestUI_Tick(run, &Fixture);
        TestUI_Tap(run, &Fixture, GameKey_Backspace);
        TEST_CHECK(run, !GameInput_IsKeyConsumed(&Fixture.Input, GameKey_Backspace) && (Get(run, &Fixture, Id) == 50.0));
        TestUI_Tap(run, &Fixture, GameKey_Menu); // Over it, not focused: the menu key is the focused widget's.
        TEST_CHECK(run, UIScreen_GetPopupCount(&Fixture.Screen) == 0U);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        Hold(&Fixture, GameKey_LeftControl, true);
        TestUI_Wheel(&Fixture, 1.0f);
        TestUI_Tick(run, &Fixture);
        Hold(&Fixture, GameKey_LeftControl, false);
        TEST_CHECK(run, Get(run, &Fixture, Id) == 50.0);
        TestUI_Press(&Fixture, GameKey_MouseMiddle); // During the left press: no reset at its release.
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseMiddle);
        TestUI_Tick(run, &Fixture);
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK_SUCCESS(run, UIValue_SetDefault(&Fixture.Screen, Id, true, 10.0));
        TestUI_MovePointer(&Fixture, MIDDLE_X, MIDDLE_Y);
        TestUI_Press(&Fixture, GameKey_MouseMiddle);
        TestUI_Tick(run, &Fixture);
        MoveTo(run, &Fixture, 900.0f, 450.0f);
        TestUI_Release(&Fixture, GameKey_MouseMiddle); // Let go off it: no reset.
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Get(run, &Fixture, Id) == 50.0);
        TEST_CHECK_SUCCESS(run, UIValue_Set(&Fixture.Screen, Id, 30.0));
        TEST_CHECK(run, UIPropertyBoxCapability_GetFillShare(&GetBase(run, &Fixture, Id)->PropertyBox) == 0.3); // 30 of 100.
        TEST_CHECK_SUCCESS(run, UIValue_SetMixed(&Fixture.Screen, Id, true));
        TEST_CHECK(run, !UIPropertyBoxCapability_ShowsFill(&GetBase(run, &Fixture, Id)->PropertyBox));

        UIPropertyBoxOptions Word = UIPropertyBoxOptions_CreateString(u8"desk");
        Word.ShowsFill = true;
        Word.HasDefaultText = true;
        Word.DefaultText = u8"chair";
        uint64_t WordId = AddBoxAt(run, &Fixture, &Word, 400.0f);
        TEST_CHECK(run, !UIPropertyBoxCapability_ShowsFill(&GetBase(run, &Fixture, WordId)->PropertyBox));
        snprintf(Clipboard.Text, sizeof(Clipboard.Text), "lamp");
        for (size_t Item = 0U; Item < 4U; Item += 3U) // Reset, then Paste, each chosen after a disable.
        {
            TEST_CHECK_SUCCESS(run, UIPropertyBox_SetIsEnabled(&Fixture.Screen, WordId, true));
            TestUI_MovePointer(&Fixture, 500.0f, MIDDLE_Y);
            TestUI_Press(&Fixture, GameKey_MouseRight);
            TestUI_Tick(run, &Fixture);
            TestUI_Release(&Fixture, GameKey_MouseRight);
            TestUI_Ticks(run, &Fixture, 3U);
            Widget* Menu = NULL;
            if (TEST_CHECK(run, (UIScreen_GetPopupCount(&Fixture.Screen) == 1U)
                && UIScreen_TryGetWidgetById(&Fixture.Screen, UIScreen_GetPopupAt(&Fixture.Screen, 0U), &Menu) && (Widget_GetSubWidgetCount(Menu) == 4U)))
            {
                TEST_CHECK_SUCCESS(run, UIPropertyBox_SetIsEnabled(&Fixture.Screen, WordId, false));
                Rectangle Choice = TestUI_GetPixelBox(run, &Fixture, Widget_GetId(Widget_GetSubWidgetAt(Menu, Item)));
                TestUI_Click(run, &Fixture, Choice.x + 4.0f, Choice.y + (Choice.height * 0.5f));
                TestUI_Tick(run, &Fixture);
            }
            const unsigned char* Text = NULL;
            TEST_CHECK_SUCCESS(run, UIPropertyBox_GetText(&Fixture.Screen, WordId, &Text, NULL));
            TEST_CHECK(run, strcmp((const char*)Text, "desk") == 0);
        }

        UIPropertyBoxOptions Long = UIPropertyBoxOptions_Create();
        Long.Unit = u8"12345678901234567890123456789012"; // 32 bytes, one past the most.
        Long.Element.HasPlacement = true;
        Long.Element.Placement = UIPlacement_FromTopLeft(100.0f, 300.0f, 200.0f, 24.0f);
        uint64_t LongId = WIDGET_ID_INVALID;
        TEST_CHECK_ERROR(run, UIKit_CreatePropertyBox(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Long, &LongId), ErrorCode_ArgumentOutOfRange);
        Long.Unit = (const unsigned char*)"\xc3\x28";
        TEST_CHECK_ERROR(run, UIKit_CreatePropertyBox(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Long, &LongId), ErrorCode_ArgumentOutOfRange);
    }
    TestUI_Deconstruct(run, &Fixture);
}


void UIPropertyBoxTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "showing_writes_its_digits_and_unit_and_never_changes_the_value", ShowingWritesItsDigitsAndUnitAndNeverChangesTheValue },
        { "a_press_becomes_a_scrub_only_past_the_threshold", APressBecomesAScrubOnlyPastTheThreshold },
        { "a_scrub_without_a_range_goes_by_the_step_or_the_magnitude", AScrubWithoutARangeGoesByTheStepOrTheMagnitude },
        { "shift_slows_a_scrub_and_ctrl_snaps_it", ShiftSlowsAScrubAndCtrlSnapsIt },
        { "soft_limits_stop_a_scrub_and_typing_goes_to_the_hard_ones", SoftLimitsStopAScrubAndTypingGoesToTheHardOnes },
        { "escape_or_a_right_press_takes_a_scrub_back", EscapeOrARightPressTakesAScrubBack },
        { "a_lost_press_commits_the_scrub", ALostPressCommitsTheScrub },
        { "a_click_without_moving_types_with_everything_selected", AClickWithoutMovingTypesWithEverythingSelected },
        { "a_double_click_types_with_everything_selected", ADoubleClickTypesWithEverythingSelected },
        { "bad_typed_text_shows_invalid_and_goes_back", BadTypedTextShowsInvalidAndGoesBack },
        { "tab_commits_and_types_in_the_next_box_either_way", TabCommitsAndTypesInTheNextBoxEitherWay },
        { "far_doubles_round_trip_through_the_text", FarDoublesRoundTripThroughTheText },
        { "mixed_shows_a_dash_until_edited", MixedShowsADashUntilEdited },
        { "a_string_types_at_once_and_posts_at_the_end", AStringTypesAtOnceAndPostsAtTheEnd },
        { "only_ctrl_with_the_wheel_steps_a_number", OnlyCtrlWithTheWheelStepsANumber },
        { "the_middle_click_and_the_reset_action_reset", TheMiddleClickAndTheResetActionReset },
        { "hover_copy_and_paste_and_hostile_clipboard_text", HoverCopyAndPasteAndHostileClipboardText },
        { "its_menu_is_reset_copy_paste_or_while_typing_a_text_boxs", ItsMenuIsResetCopyPasteOrWhileTypingATextBoxs },
        { "disabled_it_takes_a_scrub_back_and_no_focus", DisabledItTakesAScrubBackAndNoFocus },
        { "typing_over_its_own_text_keeps_every_character", TypingOverItsOwnTextKeepsEveryCharacter },
        { "mixed_becomes_one_value_by_any_edit", MixedBecomesOneValueByAnyEdit },
        { "its_menus_paste_takes_its_unit", ItsMenusPasteTakesItsUnit },
        { "its_size_follows_what_it_shows", ItsSizeFollowsWhatItShows },
        { "a_right_press_during_a_press_takes_it_back", ARightPressDuringAPressTakesItBack },
        { "a_scrubs_place_keeps_to_its_limits_and_its_start", AScrubsPlaceKeepsToItsLimitsAndItsStart },
        { "a_scrub_counts_ui_pixels", AScrubCountsUIPixels },
        { "typing_in_a_number_keeps_its_rules", TypingInANumberKeepsItsRules },
        { "what_is_not_the_boxs_passes_through", WhatIsNotTheBoxsPassesThrough },
    };
    TestRun_RunSuite(run, "UIPropertyBox", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
