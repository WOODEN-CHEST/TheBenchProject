#include "TestSuites.h"
#include "TestUI.h"
#include "GameInput.h"
#include "TextComponent.h"
#include "TextComponentFactory.h"
#include "UIContextMenu.h"
#include "UIKit.h"
#include "UIMenu.h"
#include "UIPopup.h"
#include "UIWidgetFactory.h"
#include "wr/WRMemory.h"
#include "wr/WRUserData.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
/** The fixture's look: text 16 pixels tall in the monospace font (each character 8 wide), a 4 pixel padding, 10 pixel bars. */
#define TEXT_BOX_THEME \
    "{\"name\": \"text_boxes\", \"font\": \"mono\", \"text_size\": 16, \"elements\": {" \
    "\"panel\": {\"padding\": [0, 0], \"spacing\": 0, \"background\": {\"kind\": \"flat\"}}," \
    "\"button\": {\"padding\": [8, 4], \"min_size\": [24, 24], \"background\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#e0e0e0\"}}," \
    "\"menu\": {\"padding\": [0, 0], \"item_height\": 20, \"background\": {\"kind\": \"flat\"}, \"item\": {\"kind\": \"flat\"}, " \
    "\"text\": {\"normal\": \"#ffffff\"}, \"shortcut\": {\"normal\": \"#ffffff\"}, \"separator\": {\"kind\": \"flat\"}}," \
    "\"scrollbar\": {\"thickness\": 10, \"min_thumb_length\": 10, \"thumb_inset\": 0, \"track\": {\"kind\": \"flat\"}, \"thumb\": {\"kind\": \"flat\"}}," \
    "\"text_box\": {\"padding\": [4, 4], \"min_size\": [100, 24], \"caret_width\": 1, \"mask\": \"*\", \"box\": {\"kind\": \"flat\"}," \
    "\"read_only_box\": {\"kind\": \"flat\"}, \"selection\": {\"kind\": \"flat\"}, \"inactive_selection\": {\"kind\": \"flat\"}, " \
    "\"caret\": {\"kind\": \"flat\"}, \"overwrite_caret\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#e0e0e0\"}, \"placeholder\": {\"normal\": \"#808080\"}, " \
    "\"variants\": {\"outlined\": {\"box\": {\"kind\": \"flat\", \"outline\": {\"thickness\": 6}}}}}}}"
/** Where the test boxes stand, in surface pixels, and the text's first character's left edge. */
#define BOX_X 100.0f
#define BOX_Y 100.0f
#define BOX_WIDTH 200.0f
#define LINE 16.0f
#define CHARACTER 8.0f
#define TEXT_X (BOX_X + 4.0f)
#define TEXT_Y (BOX_Y + 4.0f)


// Types.
/* A clipboard of the tests' own. */
typedef struct TestClipboardStruct
{
    char Text[256];
    /** A large text lent in place of Text when set. */
    const char* Large;
    /** The most bytes the last read asked for. */
    size_t LastMaxBytes;
} TestClipboard;


// Static functions: the clipboard.
static Error TestClipboard_GetText(void* self, GenericBuffer* outText, size_t maxBytes)
{
    TestClipboard* Clipboard = self;
    Clipboard->LastMaxBytes = maxBytes;
    const char* Text = (Clipboard->Large != NULL) ? Clipboard->Large : Clipboard->Text;
    size_t Length = strlen(Text);
    Length = (Length < maxBytes) ? Length : maxBytes;
    if ((Length > 0U) && !GenericBuffer_AppendRangeBytes(outText, (unsigned char*)Text, Length))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "test clipboard");
    }
    return Error_CreateSuccess();
}

static Error TestClipboard_SetText(void* self, const unsigned char* text)
{
    TestClipboard* Clipboard = self;
    snprintf(Clipboard->Text, sizeof(Clipboard->Text), "%s", (const char*)text);
    return Error_CreateSuccess();
}

static void TestClipboard_Destroy(void* self)
{
    (void)self;
}

static const IClipboardVTable TestClipboardVTable = { .GetText = TestClipboard_GetText, .SetText = TestClipboard_SetText,
    .Destroy = TestClipboard_Destroy };


// Static functions: helpers.
static bool Construct(TestRun* run, TestUIFixture* fixture, TestClipboard* clipboard)
{
    if (!TestUI_Construct(run, fixture) || !TestUI_SetTheme(run, fixture, TEXT_BOX_THEME))
    {
        return false;
    }
    Memory_Zero(clipboard, sizeof(*clipboard));
    GameInput_SetClipboard(&fixture->Input, (IClipboard){ .VTable = &TestClipboardVTable, .Self = clipboard });
    return true;
}

static uint64_t AddBoxAt(TestRun* run, TestUIFixture* fixture, const UITextBoxOptions* options, float y, float height)
{
    UITextBoxOptions Options = *options;
    Options.Element.HasPlacement = true;
    Options.Element.Placement = UIPlacement_FromTopLeft(BOX_X, y, BOX_WIDTH, height);
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIKit_CreateTextBox(&fixture->Kit, &fixture->Screen, WIDGET_ID_INVALID, &Options, &Id));
    return Id;
}

/* A one-line box with a text, laid out. */
static uint64_t AddLine(TestRun* run, TestUIFixture* fixture, const char* text)
{
    UITextBoxOptions Options = UITextBoxOptions_Create();
    Options.Text = (const unsigned char*)text;
    uint64_t Id = AddBoxAt(run, fixture, &Options, BOX_Y, 24.0f);
    TestUI_Ticks(run, fixture, 2U);
    return Id;
}

/* A box of several lines (four lines tall), laid out. */
static uint64_t AddLines(TestRun* run, TestUIFixture* fixture, const char* text, bool isWrapped)
{
    UITextBoxOptions Options = UITextBoxOptions_CreateMultiline();
    Options.Text = (const unsigned char*)text;
    Options.IsWrapped = isWrapped;
    if (!isWrapped)
    {
        Options.Horizontal = UIScrollMode_Auto;
    }
    uint64_t Id = AddBoxAt(run, fixture, &Options, BOX_Y, (4.0f * LINE) + 8.0f);
    TestUI_Ticks(run, fixture, 2U);
    return Id;
}

static const char* GetText(TestUIFixture* fixture, uint64_t id)
{
    const unsigned char* Text = NULL;
    Error Result = UITextBox_GetText(&fixture->Screen, id, &Text, NULL);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return "<gone>";
    }
    return (const char*)Text;
}

static bool IsText(TestUIFixture* fixture, uint64_t id, const char* expected)
{
    return strcmp(GetText(fixture, id), expected) == 0;
}

static bool IsSelection(TestUIFixture* fixture, uint64_t id, size_t anchor, size_t caret)
{
    size_t Anchor = 9999U;
    size_t Caret = 9999U;
    Error Result = UITextBox_GetSelection(&fixture->Screen, id, &Anchor, &Caret);
    bool IsSame = (Result.Code == ErrorCode_Success) && (Anchor == anchor) && (Caret == caret);
    Error_Deconstruct(&Result);
    return IsSame;
}

static UITextBoxCapability* GetBox(TestUIFixture* fixture, uint64_t id)
{
    Widget* Found = NULL;
    if (!UIScreen_TryGetWidgetById(&fixture->Screen, id, &Found))
    {
        return NULL;
    }
    return &((UITextBoxLookBase*)(void*)Found)->TextBox;
}

static bool IsEntered(TestUIFixture* fixture, uint64_t id)
{
    return UIScreen_GetEnteredWidgetId(&fixture->Screen) == id;
}

/* Types an ASCII text, a character a tick. */
static void TypeText(TestRun* run, TestUIFixture* fixture, const char* text)
{
    for (const char* Cursor = text; *Cursor != 0; Cursor++)
    {
        fixture->Sample.TextCodepoints[0] = (uint32_t)(unsigned char)*Cursor;
        fixture->Sample.TextCodepointCount = 1U;
        TestUI_Tick(run, fixture);
    }
}

static void HoldModifiers(TestUIFixture* fixture, uint8_t modifiers, bool isDown)
{
    const struct { uint8_t Flag; GameKey Key; } Keys[] =
    {
        { GAME_KEY_MODIFIER_CONTROL, GameKey_LeftControl }, { GAME_KEY_MODIFIER_SHIFT, GameKey_LeftShift }, { GAME_KEY_MODIFIER_ALT, GameKey_LeftAlt },
        { GAME_KEY_MODIFIER_SUPER, GameKey_LeftSuper }
    };
    for (size_t Index = 0U; Index < (sizeof(Keys) / sizeof(Keys[0])); Index++)
    {
        if ((modifiers & Keys[Index].Flag) != 0U)
        {
            if (isDown)
            {
                TestUI_Press(fixture, Keys[Index].Key);
            }
            else
            {
                TestUI_Release(fixture, Keys[Index].Key);
            }
        }
    }
}

/* A key pressed with modifiers for a tick, then everything let go. */
static void Chord(TestRun* run, TestUIFixture* fixture, uint8_t modifiers, GameKey key)
{
    HoldModifiers(fixture, modifiers, true);
    TestUI_Press(fixture, key);
    TestUI_Tick(run, fixture);
    TestUI_Release(fixture, key);
    HoldModifiers(fixture, modifiers, false);
    TestUI_Tick(run, fixture);
}

static void Tap(TestRun* run, TestUIFixture* fixture, GameKey key)
{
    Chord(run, fixture, GAME_KEY_MODIFIER_NONE, key);
}

/* A left press at a place, held. */
static void PressAt(TestRun* run, TestUIFixture* fixture, float x, float y)
{
    TestUI_MovePointer(fixture, x, y);
    TestUI_Tick(run, fixture);
    TestUI_Press(fixture, GameKey_MouseLeft);
    TestUI_Tick(run, fixture);
}

static void ReleaseLeft(TestRun* run, TestUIFixture* fixture)
{
    TestUI_Release(fixture, GameKey_MouseLeft);
    TestUI_Tick(run, fixture);
}

/* The surface x of the place before character @p index of a one-line box's text, a little inside it. */
static float CharacterX(size_t index)
{
    return TEXT_X + ((float)index * CHARACTER) + 1.0f;
}

static float LineY(size_t line)
{
    return TEXT_Y + ((float)line * LINE) + (LINE * 0.5f);
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

static Error RecordCue(UIScreen* screen, const UIEvent* event, const UserData* userData)
{
    (void)screen;
    TestUIEvents* Events = UserData_GetPointer(userData);
    if (Events->Count < TEST_UI_MAX_RECORDED_EVENTS)
    {
        Events->Events[Events->Count] = *event;
        Events->Count++;
    }
    return Error_CreateSuccess();
}

static bool IsEvent(const TestUIEvents* events, size_t index, uint64_t id, UIElementEvent event)
{
    return (index < events->Count) && (events->Events[index].WidgetId == id) && (events->Events[index].WidgetEventId == (int32_t)event);
}


// Tests: entering and leaving.
static void TypingOnAFocusedBoxDoesNothing(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        uint64_t Id = AddLine(run, &Fixture, "keep");
        Widget* Box = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Box)))
        {
            TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, Box));
        }
        TypeText(run, &Fixture, "xy");
        Tap(run, &Fixture, GameKey_Backspace);
        TEST_CHECK(run, IsText(&Fixture, Id, "keep") && !IsEntered(&Fixture, Id));
        // The text reaches the game.
        Fixture.Sample.TextCodepoints[0] = (uint32_t)'z';
        Fixture.Sample.TextCodepointCount = 1U;
        GameInput_BeginTick(&Fixture.Input, &Fixture.Sample);
        TEST_CHECK_SUCCESS(run, UIScreen_Update(&Fixture.Screen, ProgramTime_Create(1.0, TEST_UI_TICK_SECONDS), &Fixture.ScreenInput));
        TEST_CHECK(run, GameInput_GetTextCodepointCount(&Fixture.Input) == 1U);
        Fixture.Sample.TextCodepointCount = 0U;
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void EnterSelectsAOneLineBoxAndTypingReplacesIt(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    TestUIEvents Events;
    if (Construct(run, &Fixture, &Clipboard) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddLine(run, &Fixture, "old");
        Widget* Box = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Box)))
        {
            TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, Box));
        }
        Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, IsEntered(&Fixture, Id) && IsSelection(&Fixture, Id, 0U, 3U));
        TypeText(run, &Fixture, "new");
        TEST_CHECK(run, IsText(&Fixture, Id, "new") && IsSelection(&Fixture, Id, 3U, 3U));
        TEST_CHECK(run, CountEvents(&Events, Id, UIElementEvent_Changed) == 3U);
        TEST_CHECK(run, CountEvents(&Events, Id, UIElementEvent_Committed) == 0U);
        // Enter keeps and leaves: one Committed for the visit, carrying the length.
        Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, !IsEntered(&Fixture, Id) && (CountEvents(&Events, Id, UIElementEvent_Committed) == 1U));
        TEST_CHECK(run, IsEvent(&Events, Events.Count - 1U, Id, UIElementEvent_Committed) && (Events.Events[Events.Count - 1U].Value.Data.DoubleValue == 3.0));
        // A visit that changed nothing commits nothing.
        Tap(run, &Fixture, GameKey_Enter);
        Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, CountEvents(&Events, Id, UIElementEvent_Committed) == 1U);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void EscapeRevertsOneLineAndKeepsSeveral(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    TestUIEvents Events;
    if (Construct(run, &Fixture, &Clipboard) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddLine(run, &Fixture, "first");
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TestUI_Tick(run, &Fixture);
        TypeText(run, &Fixture, "!!");
        TEST_CHECK(run, IsText(&Fixture, Id, "first!!"));
        Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, IsText(&Fixture, Id, "first") && !IsEntered(&Fixture, Id));
        TEST_CHECK(run, (CountEvents(&Events, Id, UIElementEvent_Cancelled) == 1U) && (CountEvents(&Events, Id, UIElementEvent_Committed) == 0U));
        // The typing undone by the cancel can be redone.
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TestUI_Tick(run, &Fixture);
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_Y);
        TEST_CHECK(run, IsText(&Fixture, Id, "first!!"));
        Tap(run, &Fixture, GameKey_Escape);

        // Several lines: Escape keeps the text and commits it.
        Events.Count = 0U;
        UITextBoxOptions Options = UITextBoxOptions_CreateMultiline();
        Options.Text = (const unsigned char*)"a";
        uint64_t Lines = AddBoxAt(run, &Fixture, &Options, 200.0f, 72.0f);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Lines));
        TestUI_Tick(run, &Fixture);
        TypeText(run, &Fixture, "b");
        Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, IsText(&Fixture, Lines, "ab") && (CountEvents(&Events, Lines, UIElementEvent_Committed) == 1U)
            && (CountEvents(&Events, Lines, UIElementEvent_Cancelled) == 0U));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void APressPlacesTheCaretAndCodeLeavesIt(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        uint64_t Id = AddLine(run, &Fixture, "abcdef");
        PressAt(run, &Fixture, CharacterX(2U), LineY(0U));
        ReleaseLeft(run, &Fixture);
        TEST_CHECK(run, IsEntered(&Fixture, Id) && IsSelection(&Fixture, Id, 2U, 2U));
        // The right half of a character places the caret after it.
        PressAt(run, &Fixture, TEXT_X + (3.0f * CHARACTER) + 6.0f, LineY(0U));
        ReleaseLeft(run, &Fixture);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 4U, 4U));
        TypeText(run, &Fixture, "X");
        TEST_CHECK(run, IsText(&Fixture, Id, "abcdXef"));
        // Code enters it with the caret where it was.
        Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK_SUCCESS(run, UITextBox_SetSelection(&Fixture.Screen, Id, 1U, 1U));
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsEntered(&Fixture, Id) && IsSelection(&Fixture, Id, 1U, 1U));
        // A box of several lines entered by Enter puts the caret at the end.
        uint64_t Lines = AddLines(run, &Fixture, "one\ntwo", true);
        Widget* Box = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Lines, &Box)))
        {
            TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, Box));
        }
        Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, IsEntered(&Fixture, Lines) && IsSelection(&Fixture, Lines, 7U, 7U));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void TabCarriesOnIntoTheNextBoxSelected(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    TestUIEvents Events;
    if (Construct(run, &Fixture, &Clipboard) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UITextBoxOptions Options = UITextBoxOptions_Create();
        Options.Text = (const unsigned char*)"one";
        uint64_t First = AddBoxAt(run, &Fixture, &Options, 100.0f, 24.0f);
        Options.Text = (const unsigned char*)"two";
        uint64_t Second = AddBoxAt(run, &Fixture, &Options, 140.0f, 24.0f);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, First));
        TestUI_Tick(run, &Fixture);
        TypeText(run, &Fixture, "!");
        Tap(run, &Fixture, GameKey_Tab);
        TEST_CHECK(run, IsEntered(&Fixture, Second) && IsSelection(&Fixture, Second, 0U, 3U));
        TEST_CHECK(run, CountEvents(&Events, First, UIElementEvent_Committed) == 1U);
        TypeText(run, &Fixture, "2");
        TEST_CHECK(run, IsText(&Fixture, Second, "2"));
        Chord(run, &Fixture, GAME_KEY_MODIFIER_SHIFT, GameKey_Tab);
        TEST_CHECK(run, IsEntered(&Fixture, First) && IsSelection(&Fixture, First, 0U, 4U));
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Tests: the keys.
static void TheArrowsHomeAndEndMoveTheCaret(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        uint64_t Id = AddLine(run, &Fixture, "one two_three");
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TestUI_Tick(run, &Fixture);
        Tap(run, &Fixture, GameKey_Home);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 0U, 0U));
        Tap(run, &Fixture, GameKey_Right);
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_Right);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 3U, 3U));
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_Right);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 13U, 13U));
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL | GAME_KEY_MODIFIER_SHIFT, GameKey_Left);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 13U, 4U));
        // Left collapses the selection to its start, Right to its end.
        Tap(run, &Fixture, GameKey_Left);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 4U, 4U));
        Chord(run, &Fixture, GAME_KEY_MODIFIER_SHIFT, GameKey_End);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 4U, 13U));
        Tap(run, &Fixture, GameKey_Right);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 13U, 13U));
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_Home);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 0U, 0U));
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL | GAME_KEY_MODIFIER_SHIFT, GameKey_End);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 0U, 13U));
        // Up and Down on one line are left to whoever is around the box; the screen keeps them from the game.
        Tap(run, &Fixture, GameKey_Up);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 0U, 13U) && IsEntered(&Fixture, Id));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void BackspaceAndDeleteByCharacterAndWord(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        uint64_t Id = AddLine(run, &Fixture, "alpha beta gamma");
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TestUI_Tick(run, &Fixture);
        Tap(run, &Fixture, GameKey_End);
        Tap(run, &Fixture, GameKey_Backspace);
        TEST_CHECK(run, IsText(&Fixture, Id, "alpha beta gamm"));
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_Backspace);
        TEST_CHECK(run, IsText(&Fixture, Id, "alpha beta "));
        Tap(run, &Fixture, GameKey_Home);
        Tap(run, &Fixture, GameKey_Delete);
        TEST_CHECK(run, IsText(&Fixture, Id, "lpha beta "));
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_Delete);
        TEST_CHECK(run, IsText(&Fixture, Id, " beta "));
        // A held Backspace repeats.
        Tap(run, &Fixture, GameKey_End);
        TestUI_Press(&Fixture, GameKey_Backspace);
        TestUI_Tick(run, &Fixture);
        TestUI_Repeat(&Fixture, GameKey_Backspace);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_Backspace);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsText(&Fixture, Id, " bet"));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void InsertTurnsOverwriteOnUntilTheBoxIsLeft(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        uint64_t Id = AddLine(run, &Fixture, "abcd");
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TestUI_Tick(run, &Fixture);
        Tap(run, &Fixture, GameKey_Home);
        Tap(run, &Fixture, GameKey_Insert);
        TEST_CHECK(run, UITextBoxCapability_IsOverwriting(GetBox(&Fixture, Id)));
        // Held, it switches once: its repeats do nothing.
        TestUI_Press(&Fixture, GameKey_Insert);
        TestUI_Tick(run, &Fixture);
        TestUI_Repeat(&Fixture, GameKey_Insert);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_Insert);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, !UITextBoxCapability_IsOverwriting(GetBox(&Fixture, Id)));
        Tap(run, &Fixture, GameKey_Insert);
        TEST_CHECK(run, UITextBoxCapability_IsOverwriting(GetBox(&Fixture, Id)));
        TypeText(run, &Fixture, "XY");
        TEST_CHECK(run, IsText(&Fixture, Id, "XYcd"));
        // One undo takes the overwriting back.
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_Z);
        TEST_CHECK(run, IsText(&Fixture, Id, "abcd"));
        Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, !UITextBoxCapability_IsOverwriting(GetBox(&Fixture, Id)));
        // A box that forbids it.
        TEST_CHECK_SUCCESS(run, UITextBox_SetKeys(&Fixture.Screen, Id, false, false, true));
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TestUI_Tick(run, &Fixture);
        Tap(run, &Fixture, GameKey_Insert);
        TEST_CHECK(run, !UITextBoxCapability_IsOverwriting(GetBox(&Fixture, Id)));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void TypingGroupsIntoUndoSteps(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        uint64_t Id = AddLine(run, &Fixture, "");
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TestUI_Tick(run, &Fixture);
        TypeText(run, &Fixture, "hello");
        // A pause of more than a second starts a new step.
        TestUI_Ticks(run, &Fixture, 500U);
        TypeText(run, &Fixture, " world");
        TEST_CHECK(run, IsText(&Fixture, Id, "hello world"));
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_Z);
        TEST_CHECK(run, IsText(&Fixture, Id, "hello"));
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL | GAME_KEY_MODIFIER_SHIFT, GameKey_Z);
        TEST_CHECK(run, IsText(&Fixture, Id, "hello world"));
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_Z);
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_Z);
        TEST_CHECK(run, IsText(&Fixture, Id, ""));
        // Nothing left: the key is still the box's while it is entered, and reaches nothing behind it.
        HoldModifiers(&Fixture, GAME_KEY_MODIFIER_CONTROL, true);
        TestUI_Press(&Fixture, GameKey_Z);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, !GameInput_IsKeyPressed(&Fixture.Input, GameKey_Z) && IsEntered(&Fixture, Id));
        TestUI_Release(&Fixture, GameKey_Z);
        HoldModifiers(&Fixture, GAME_KEY_MODIFIER_CONTROL, false);
        TestUI_Tick(run, &Fixture);
        // Not entered, the undo key goes on.
        Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, !IsEntered(&Fixture, Id));
        HoldModifiers(&Fixture, GAME_KEY_MODIFIER_CONTROL, true);
        TestUI_Press(&Fixture, GameKey_Z);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, GameInput_IsKeyPressed(&Fixture.Input, GameKey_Z));
        TestUI_Release(&Fixture, GameKey_Z);
        HoldModifiers(&Fixture, GAME_KEY_MODIFIER_CONTROL, false);
        TestUI_Tick(run, &Fixture);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void TheClipboardKeysCopyCutAndPaste(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        uint64_t Id = AddLine(run, &Fixture, "copy me");
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TestUI_Tick(run, &Fixture);
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_A);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 0U, 7U));
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_C);
        TEST_CHECK(run, strcmp(Clipboard.Text, "copy me") == 0);
        TEST_CHECK_SUCCESS(run, UITextBox_SetSelection(&Fixture.Screen, Id, 0U, 4U));
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_X);
        TEST_CHECK(run, IsText(&Fixture, Id, " me") && (strcmp(Clipboard.Text, "copy") == 0));
        Tap(run, &Fixture, GameKey_End);
        Chord(run, &Fixture, GAME_KEY_MODIFIER_SHIFT, GameKey_Insert);
        TEST_CHECK(run, IsText(&Fixture, Id, " mecopy"));
        // A hostile paste: line breaks become spaces on one line, invalid UTF-8 the replacement character.
        snprintf(Clipboard.Text, sizeof(Clipboard.Text), "a\nb\xFF");
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_V);
        TEST_CHECK(run, IsText(&Fixture, Id, " mecopya b\xEF\xBF\xBD"));
        // The paste is a step of its own.
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_Z);
        TEST_CHECK(run, IsText(&Fixture, Id, " mecopy"));
        // Ctrl+Insert copies, Shift+Delete cuts.
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_A);
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_Insert);
        TEST_CHECK(run, strcmp(Clipboard.Text, " mecopy") == 0);
        Chord(run, &Fixture, GAME_KEY_MODIFIER_SHIFT, GameKey_Delete);
        TEST_CHECK(run, IsText(&Fixture, Id, ""));
    }
    TestUI_Deconstruct(run, &Fixture);
}

// Tests: several lines.
static void UpAndDownKeepTheColumnAcrossShortLines(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        uint64_t Id = AddLines(run, &Fixture, "abcdefgh\nab\nabcdefgh", false);
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TEST_CHECK_SUCCESS(run, UITextBox_SetSelection(&Fixture.Screen, Id, 6U, 6U));
        TestUI_Tick(run, &Fixture);
        Tap(run, &Fixture, GameKey_Down);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 11U, 11U));
        Tap(run, &Fixture, GameKey_Down);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 18U, 18U));
        Tap(run, &Fixture, GameKey_Up);
        Tap(run, &Fixture, GameKey_Up);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 6U, 6U));
        // Up on the first line goes to the start, Down on the last to the end; Shift extends.
        Chord(run, &Fixture, GAME_KEY_MODIFIER_SHIFT, GameKey_Up);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 6U, 0U));
        TEST_CHECK_SUCCESS(run, UITextBox_SetSelection(&Fixture.Screen, Id, 14U, 14U));
        Tap(run, &Fixture, GameKey_Down);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 20U, 20U));
        // Enter is a new line here; Ctrl+Enter keeps and leaves.
        Tap(run, &Fixture, GameKey_Enter);
        TypeText(run, &Fixture, "x");
        TEST_CHECK(run, IsText(&Fixture, Id, "abcdefgh\nab\nabcdefgh\nx") && IsEntered(&Fixture, Id));
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_Enter);
        TEST_CHECK(run, !IsEntered(&Fixture, Id) && IsText(&Fixture, Id, "abcdefgh\nab\nabcdefgh\nx"));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void HomeAndEndKeepToTheWrappedLine(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        // 190 pixels of room wrap after 23 characters: "aaaa bbbb cccc dddd" then "eeee ffff".
        uint64_t Id = AddLines(run, &Fixture, "aaaa bbbb cccc dddd eeee ffff", true);
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TEST_CHECK_SUCCESS(run, UITextBox_SetSelection(&Fixture.Screen, Id, 2U, 2U));
        TestUI_Tick(run, &Fixture);
        Tap(run, &Fixture, GameKey_End);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 19U, 19U));
        Tap(run, &Fixture, GameKey_Down);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 29U, 29U));
        Tap(run, &Fixture, GameKey_Home);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 20U, 20U));
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_Home);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 0U, 0U));
        // A press on the second line places the caret on it.
        PressAt(run, &Fixture, CharacterX(2U), LineY(1U));
        ReleaseLeft(run, &Fixture);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 22U, 22U));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void PageKeysMoveByTheView(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        uint64_t Id = AddLines(run, &Fixture, "0\n1\n2\n3\n4\n5\n6\n7\n8\n9", false);
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TEST_CHECK_SUCCESS(run, UITextBox_SetSelection(&Fixture.Screen, Id, 0U, 0U));
        TestUI_Ticks(run, &Fixture, 2U);
        Tap(run, &Fixture, GameKey_PageDown);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 8U, 8U));
        Vector2 Offset = { 0 };
        TEST_CHECK_SUCCESS(run, UIScrollable_GetOffset(&Fixture.Screen, Id, &Offset));
        TEST_CHECK(run, Offset.y > 0.0f);
        Tap(run, &Fixture, GameKey_PageUp);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 0U, 0U));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIScrollable_GetOffset(&Fixture.Screen, Id, &Offset));
        TEST_CHECK(run, Offset.y == 0.0f);
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Tests: what is around a box.
typedef struct KeyProbeStruct
{
    Widget Base;
    int32_t Ups;
    int32_t Downs;
    int32_t ControlEnters;
    int32_t Es;
    int32_t DownRepeats;
} KeyProbe;

static Error KeyProbe_Construct(void* widget, void* args)
{
    (void)args;
    KeyProbe* Probe = widget;
    Probe->Ups = 0;
    Probe->Downs = 0;
    Probe->ControlEnters = 0;
    Probe->Es = 0;
    Probe->DownRepeats = 0;
    return Error_CreateSuccess();
}

static Error KeyProbe_OnKeyboardInput(void* self, const WidgetKeyboardInputArgs* args, bool* outIsHandled)
{
    KeyProbe* Probe = self;
    if ((args->Type == UIKeyboardInputType_KeyRepeat) && (args->Key == GameKey_Down))
    {
        Probe->DownRepeats++;
        *outIsHandled = true;
        return Error_CreateSuccess();
    }
    if (args->Type != UIKeyboardInputType_KeyPress)
    {
        return Error_CreateSuccess();
    }
    bool IsControl = (args->Modifiers & GAME_KEY_MODIFIER_CONTROL) != 0U;
    Probe->Ups += (args->Key == GameKey_Up) ? 1 : 0;
    Probe->Downs += (args->Key == GameKey_Down) ? 1 : 0;
    Probe->ControlEnters += ((args->Key == GameKey_Enter) && IsControl) ? 1 : 0;
    Probe->Es += (args->Key == GameKey_E) ? 1 : 0;
    *outIsHandled = (args->Key == GameKey_Up) || (args->Key == GameKey_Down) || ((args->Key == GameKey_Enter) && IsControl) || (args->Key == GameKey_E);
    return Error_CreateSuccess();
}

static const WidgetVTable KeyProbeVTable = { .OnKeyboardInput = KeyProbe_OnKeyboardInput };

static uint64_t AddKeyProbe(TestRun* run, TestUIFixture* fixture)
{
    uint64_t TypeId = WIDGET_TYPE_ID_INVALID;
    WidgetTypeDescriptor Descriptor = { .Name = u8"KeyProbe", .StructSize = sizeof(KeyProbe), .VTable = &KeyProbeVTable,
        .Constructor = KeyProbe_Construct, .IsFocusable = false };
    Widget* Created = NULL;
    if (!TEST_CHECK_SUCCESS(run, UIWidgetFactory_RegisterType(&fixture->Widgets, &Descriptor, &TypeId))
        || !TEST_CHECK_SUCCESS(run, UIWidgetFactory_CreateWidget(&fixture->Widgets, &fixture->Screen, TypeId, NULL, &Created)))
    {
        return WIDGET_ID_INVALID;
    }
    uint64_t Id = Widget_GetId(Created);
    UIPlacement Placement = UIPlacement_Stretch(UIInsets_All(0.0f));
    TEST_CHECK_SUCCESS(run, Widget_SetPlacement(Created, &Placement));
    TEST_CHECK_SUCCESS(run, Widget_InitializeWidget(Created));
    TEST_CHECK_SUCCESS(run, UIScreen_AddWidget(&fixture->Screen, Created));
    return Id;
}

static KeyProbe* GetKeyProbe(TestUIFixture* fixture, uint64_t id)
{
    Widget* Found = NULL;
    return UIScreen_TryGetWidgetById(&fixture->Screen, id, &Found) ? (KeyProbe*)(void*)Found : NULL;
}

static void WhatABoxLeavesReachesItsOwner(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        uint64_t ProbeId = AddKeyProbe(run, &Fixture);
        UITextBoxOptions Options = UITextBoxOptions_Create();
        Options.Text = (const unsigned char*)"history";
        Options.Element.HasPlacement = true;
        Options.Element.Placement = UIPlacement_FromTopLeft(BOX_X, BOX_Y, BOX_WIDTH, 24.0f);
        uint64_t Id = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateTextBox(&Fixture.Kit, &Fixture.Screen, ProbeId, &Options, &Id));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TestUI_Tick(run, &Fixture);
        // A console's history takes Up and Down from a one-line box; a dialog's default takes Ctrl+Enter.
        Tap(run, &Fixture, GameKey_Up);
        Tap(run, &Fixture, GameKey_Down);
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_Enter);
        KeyProbe* Probe = GetKeyProbe(&Fixture, ProbeId);
        TEST_CHECK(run, (Probe != NULL) && (Probe->Ups == 1) && (Probe->Downs == 1) && (Probe->ControlEnters == 1));
        TEST_CHECK(run, IsEntered(&Fixture, Id));
        // Left and Right stay the box's.
        Tap(run, &Fixture, GameKey_Left);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 0U, 0U) || IsSelection(&Fixture, Id, 6U, 6U));
        // A letter's press is the box's while it types: alone, or with Ctrl and Alt together (AltGr); with Ctrl, Alt or Super it is a
        // shortcut, and goes on to the owner.
        Tap(run, &Fixture, GameKey_E);
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL | GAME_KEY_MODIFIER_ALT, GameKey_E);
        TEST_CHECK(run, (Probe != NULL) && (Probe->Es == 0));
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_E);
        Chord(run, &Fixture, GAME_KEY_MODIFIER_ALT, GameKey_E);
        Chord(run, &Fixture, GAME_KEY_MODIFIER_SUPER, GameKey_E);
        Chord(run, &Fixture, GAME_KEY_MODIFIER_SUPER | GAME_KEY_MODIFIER_CONTROL | GAME_KEY_MODIFIER_ALT, GameKey_E);
        TEST_CHECK(run, (Probe != NULL) && (Probe->Es == 4) && IsEntered(&Fixture, Id));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void AKeyHeldPastLeavingDoesNothingMore(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        uint64_t ProbeId = AddKeyProbe(run, &Fixture);
        UITextBoxOptions Options = UITextBoxOptions_CreateMultiline();
        Options.Text = (const unsigned char*)"a\nb\nc";
        Options.Element.HasPlacement = true;
        Options.Element.Placement = UIPlacement_FromTopLeft(BOX_X, BOX_Y, BOX_WIDTH, (4.0f * LINE) + 8.0f);
        uint64_t Id = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateTextBox(&Fixture.Kit, &Fixture.Screen, ProbeId, &Options, &Id));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TEST_CHECK_SUCCESS(run, UITextBox_SetSelection(&Fixture.Screen, Id, 0U, 0U));
        TestUI_Tick(run, &Fixture);
        // Down is the box's; left while it is held, its repeats reach nobody.
        TestUI_Press(&Fixture, GameKey_Down);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 2U, 2U));
        TEST_CHECK_SUCCESS(run, UIScreen_LeaveWidget(&Fixture.Screen, UILeaveReason_Keep));
        TestUI_Repeat(&Fixture, GameKey_Down);
        TestUI_Tick(run, &Fixture);
        TestUI_Repeat(&Fixture, GameKey_Down);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_Down);
        TestUI_Tick(run, &Fixture);
        KeyProbe* Probe = GetKeyProbe(&Fixture, ProbeId);
        TEST_CHECK(run, (Probe != NULL) && (Probe->DownRepeats == 0) && IsSelection(&Fixture, Id, 2U, 2U) && !IsEntered(&Fixture, Id));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ALetterBoundToBackTypesTheLetter(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        GameInputBinding Q = { .Key = GameKey_Q, .Modifiers = GAME_KEY_MODIFIER_NONE };
        TEST_CHECK_SUCCESS(run, GameInput_SetBindings(&Fixture.Input, GameAction_UIBack, &Q, 1U));
        uint64_t Id = AddLine(run, &Fixture, "");
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TestUI_Tick(run, &Fixture);
        // The press of Q and the text it types come in the same tick.
        TestUI_Press(&Fixture, GameKey_Q);
        Fixture.Sample.TextCodepoints[0] = (uint32_t)'q';
        Fixture.Sample.TextCodepointCount = 1U;
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_Q);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsText(&Fixture, Id, "q") && IsEntered(&Fixture, Id));
        TEST_CHECK(run, !GameInput_IsActionPressed(&Fixture.Input, GameAction_UIBack));
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Tests: the mouse.
static void ADragSelectsAndShiftExtends(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        uint64_t Id = AddLine(run, &Fixture, "select this text");
        PressAt(run, &Fixture, CharacterX(7U), LineY(0U));
        TestUI_MovePointer(&Fixture, CharacterX(11U), LineY(0U));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 7U, 11U));
        TestUI_MovePointer(&Fixture, CharacterX(2U), LineY(0U));
        TestUI_Tick(run, &Fixture);
        ReleaseLeft(run, &Fixture);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 7U, 2U));
        // Shift with a press moves the caret and keeps the anchor.
        HoldModifiers(&Fixture, GAME_KEY_MODIFIER_SHIFT, true);
        PressAt(run, &Fixture, CharacterX(16U) + 20.0f, LineY(0U));
        ReleaseLeft(run, &Fixture);
        HoldModifiers(&Fixture, GAME_KEY_MODIFIER_SHIFT, false);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 7U, 16U));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void DoubleAndTripleClicksSelectWordsAndLines(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        uint64_t Id = AddLines(run, &Fixture, "one two three\nsecond line", false);
        float X = CharacterX(5U);
        // A double click takes the word; dragging on takes whole words.
        PressAt(run, &Fixture, X, LineY(0U));
        ReleaseLeft(run, &Fixture);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 4U, 7U));
        TestUI_MovePointer(&Fixture, CharacterX(10U), LineY(0U));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 4U, 13U));
        TestUI_MovePointer(&Fixture, CharacterX(1U), LineY(0U));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 7U, 0U));
        ReleaseLeft(run, &Fixture);
        // A triple click takes the line up to its break; dragging on takes whole lines.
        TestUI_Ticks(run, &Fixture, 400U);
        TestUI_MovePointer(&Fixture, X, LineY(0U));
        for (int32_t Click = 0; Click < 3; Click++)
        {
            TestUI_Press(&Fixture, GameKey_MouseLeft);
            TestUI_Tick(run, &Fixture);
            if (Click < 2)
            {
                ReleaseLeft(run, &Fixture);
            }
        }
        TEST_CHECK(run, IsSelection(&Fixture, Id, 0U, 13U));
        TestUI_MovePointer(&Fixture, X, LineY(1U));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 0U, 25U));
        ReleaseLeft(run, &Fixture);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ADragPastAnEdgeScrolls(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        // Forty characters in a view twenty-four wide.
        uint64_t Id = AddLine(run, &Fixture, "0123456789012345678901234567890123456789");
        PressAt(run, &Fixture, CharacterX(1U), LineY(0U));
        TestUI_MovePointer(&Fixture, BOX_X + BOX_WIDTH + 40.0f, LineY(0U));
        TestUI_Ticks(run, &Fixture, 30U);
        Vector2 Near = { 0 };
        TEST_CHECK_SUCCESS(run, UIScrollable_GetOffset(&Fixture.Screen, Id, &Near));
        TEST_CHECK(run, Near.x > 0.0f);
        size_t Anchor = 0U;
        size_t Caret = 0U;
        TEST_CHECK_SUCCESS(run, UITextBox_GetSelection(&Fixture.Screen, Id, &Anchor, &Caret));
        TEST_CHECK(run, (Anchor == 1U) && (Caret > 24U));
        // Further out, faster.
        TEST_CHECK_SUCCESS(run, UIScrollable_SetOffset(&Fixture.Screen, Id, (Vector2){ 0.0f, 0.0f }));
        TestUI_MovePointer(&Fixture, BOX_X + BOX_WIDTH + 200.0f, LineY(0U));
        TestUI_Ticks(run, &Fixture, 30U);
        Vector2 Far = { 0 };
        TEST_CHECK_SUCCESS(run, UIScrollable_GetOffset(&Fixture.Screen, Id, &Far));
        TEST_CHECK(run, Far.x > Near.x);
        ReleaseLeft(run, &Fixture);
        // Let go, nothing scrolls on.
        TestUI_Ticks(run, &Fixture, 10U);
        Vector2 After = { 0 };
        TEST_CHECK_SUCCESS(run, UIScrollable_GetOffset(&Fixture.Screen, Id, &After));
        TEST_CHECK(run, After.x == Far.x);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static size_t CountPopups(TestUIFixture* fixture)
{
    return UIScreen_GetPopupCount(&fixture->Screen);
}

static void ARightClickOpensTheMenu(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        uint64_t Id = AddLine(run, &Fixture, "menu text");
        TEST_CHECK_SUCCESS(run, UITextBox_SetSelection(&Fixture.Screen, Id, 0U, 4U));
        // A right press outside the selection moves the caret there first.
        TestUI_MovePointer(&Fixture, CharacterX(7U), LineY(0U));
        TestUI_Tick(run, &Fixture);
        TestUI_Press(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 7U, 7U));
        TestUI_Release(&Fixture, GameKey_MouseRight);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, CountPopups(&Fixture) == 1U);
        // Nothing selected and nothing to undo: Paste is the first item that acts.
        snprintf(Clipboard.Text, sizeof(Clipboard.Text), "!");
        Tap(run, &Fixture, GameKey_Down);
        Tap(run, &Fixture, GameKey_Enter);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsText(&Fixture, Id, "menu te!xt") && (CountPopups(&Fixture) == 0U));
        // A right press inside the selection keeps it: Cut is then the first item that acts, after Undo.
        TEST_CHECK_SUCCESS(run, UITextBox_SetSelection(&Fixture.Screen, Id, 0U, 4U));
        TestUI_MovePointer(&Fixture, CharacterX(2U), LineY(0U));
        TestUI_Tick(run, &Fixture);
        TestUI_Press(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseRight);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 0U, 4U) && (CountPopups(&Fixture) == 1U));
        Tap(run, &Fixture, GameKey_Down);
        Tap(run, &Fixture, GameKey_Down);
        Tap(run, &Fixture, GameKey_Enter);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsText(&Fixture, Id, " te!xt") && (strcmp(Clipboard.Text, "menu") == 0));
        // A masked box's menu has Cut and Copy disabled: over a selection, the first items that act are Undo and Paste.
        TEST_CHECK_SUCCESS(run, UITextBox_SetIsMasked(&Fixture.Screen, Id, true));
        TEST_CHECK_SUCCESS(run, UITextBox_SetSelection(&Fixture.Screen, Id, 0U, 6U));
        snprintf(Clipboard.Text, sizeof(Clipboard.Text), "pass");
        TestUI_MovePointer(&Fixture, CharacterX(2U), LineY(0U));
        TestUI_Tick(run, &Fixture);
        TestUI_Press(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseRight);
        TestUI_Ticks(run, &Fixture, 2U);
        Tap(run, &Fixture, GameKey_Down);
        Tap(run, &Fixture, GameKey_Down);
        Tap(run, &Fixture, GameKey_Enter);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsText(&Fixture, Id, "pass") && (strcmp(Clipboard.Text, "pass") == 0));
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Tests: not entered.
static void TheShortListCopiesPastesAndResetsTheWholeText(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    TestUIEvents Events;
    if (Construct(run, &Fixture, &Clipboard) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UITextBoxOptions Options = UITextBoxOptions_Create();
        Options.Text = (const unsigned char*)"whole";
        Options.HasDefault = true;
        Options.Default = (const unsigned char*)"default";
        uint64_t Id = AddBoxAt(run, &Fixture, &Options, BOX_Y, 24.0f);
        TestUI_Ticks(run, &Fixture, 2U);
        // Hovered, not focused: copy takes the whole text.
        TestUI_MovePointer(&Fixture, CharacterX(1U), LineY(0U));
        TestUI_Tick(run, &Fixture);
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_C);
        TEST_CHECK(run, strcmp(Clipboard.Text, "whole") == 0);
        // Paste replaces it as one whole change and one undo step.
        snprintf(Clipboard.Text, sizeof(Clipboard.Text), "pasted");
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_V);
        TEST_CHECK(run, IsText(&Fixture, Id, "pasted"));
        TEST_CHECK(run, (CountEvents(&Events, Id, UIElementEvent_Changed) == 1U) && (CountEvents(&Events, Id, UIElementEvent_Committed) == 1U));
        TEST_CHECK(run, IsEvent(&Events, Events.Count - 2U, Id, UIElementEvent_Changed) && IsEvent(&Events, Events.Count - 1U, Id, UIElementEvent_Committed));
        // Reset goes back to the default.
        Tap(run, &Fixture, GameKey_Backspace);
        TEST_CHECK(run, IsText(&Fixture, Id, "default") && !IsEntered(&Fixture, Id));
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TestUI_Tick(run, &Fixture);
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_Z);
        TEST_CHECK(run, IsText(&Fixture, Id, "pasted"));
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_Z);
        TEST_CHECK(run, IsText(&Fixture, Id, "whole"));
        Tap(run, &Fixture, GameKey_Escape);
        // Without a default, reset goes on past the box.
        TEST_CHECK_SUCCESS(run, UITextBox_SetDefault(&Fixture.Screen, Id, false, NULL));
        TestUI_Press(&Fixture, GameKey_Backspace);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, GameInput_IsKeyPressed(&Fixture.Input, GameKey_Backspace));
        TestUI_Release(&Fixture, GameKey_Backspace);
        TestUI_Tick(run, &Fixture);
        // A masked box refuses to copy.
        TEST_CHECK_SUCCESS(run, UITextBox_SetIsMasked(&Fixture.Screen, Id, true));
        snprintf(Clipboard.Text, sizeof(Clipboard.Text), "kept");
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_C);
        TEST_CHECK(run, strcmp(Clipboard.Text, "kept") == 0);
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Tests: read-only and masked.
static void AReadOnlyBoxSelectsCopiesAndScrolls(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        UITextBoxOptions Options = UITextBoxOptions_CreateMultiline();
        Options.IsEditable = false;
        Options.IsWrapped = false;
        Options.Text = (const unsigned char*)"line 0\nline 1\nline 2\nline 3\nline 4\nline 5\nline 6\nline 7";
        uint64_t Id = AddBoxAt(run, &Fixture, &Options, BOX_Y, 72.0f);
        TestUI_Ticks(run, &Fixture, 2U);
        PressAt(run, &Fixture, CharacterX(0U), LineY(0U));
        TestUI_MovePointer(&Fixture, CharacterX(4U), LineY(0U));
        TestUI_Tick(run, &Fixture);
        ReleaseLeft(run, &Fixture);
        UITextBoxCapability* Box = GetBox(&Fixture, Id);
        Widget* BoxWidget = NULL;
        UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &BoxWidget);
        TEST_CHECK(run, IsEntered(&Fixture, Id) && IsSelection(&Fixture, Id, 0U, 4U));
        TEST_CHECK(run, (Box != NULL) && (BoxWidget != NULL) && !UITextBoxCapability_IsCaretShown(Box, BoxWidget));
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_C);
        TEST_CHECK(run, strcmp(Clipboard.Text, "line") == 0);
        // Typing and deleting do nothing; the keys scroll it.
        TypeText(run, &Fixture, "x");
        Tap(run, &Fixture, GameKey_Backspace);
        TEST_CHECK(run, strncmp(GetText(&Fixture, Id), "line 0", 6U) == 0);
        Tap(run, &Fixture, GameKey_Down);
        TestUI_Tick(run, &Fixture);
        Vector2 Offset = { 0 };
        TEST_CHECK_SUCCESS(run, UIScrollable_GetOffset(&Fixture.Screen, Id, &Offset));
        TEST_CHECK(run, Offset.y == LINE);
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_End);
        TestUI_Tick(run, &Fixture);
        Vector2 Range = { 0 };
        TEST_CHECK_SUCCESS(run, UIScrollable_GetOffset(&Fixture.Screen, Id, &Offset));
        TEST_CHECK_SUCCESS(run, UIScrollable_GetRange(&Fixture.Screen, Id, &Range));
        TEST_CHECK(run, (Offset.y == Range.y) && (Range.y > 0.0f));
        // Not selectable, it takes no focus.
        Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK_SUCCESS(run, UITextBox_SetIsSelectable(&Fixture.Screen, Id, false));
        TEST_CHECK(run, (BoxWidget != NULL) && !Widget_IsFocusable(BoxWidget));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void AMaskedBoxHidesItsTextAndIsOneWord(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        UITextBoxOptions Options = UITextBoxOptions_Create();
        Options.IsMasked = true;
        Options.Text = (const unsigned char*)"pass word";
        uint64_t Id = AddBoxAt(run, &Fixture, &Options, BOX_Y, 24.0f);
        TestUI_Ticks(run, &Fixture, 2U);
        UITextBoxCapability* Box = GetBox(&Fixture, Id);
        TextComponent* Root = NULL;
        if (TEST_CHECK(run, Box != NULL)
            && TEST_CHECK_SUCCESS(run, UITextBoxCapability_BuildDocument(Box, &Fixture.Components, 0x2022U, RenderColor_White(), &Root)))
        {
            // One mask character (three bytes) per character.
            const StringComponent* Shown = TextComponent_AsConstStringComponent(TextComponent_GetSubComponentAt(Root, 0U));
            TEST_CHECK(run, (Shown != NULL) && (strlen((const char*)StringComponent_GetText(Shown)) == 27U));
            TEST_CHECK_SUCCESS(run, TextComponentFactory_ReturnTree(&Fixture.Components, Root));
        }
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TestUI_Tick(run, &Fixture);
        Tap(run, &Fixture, GameKey_End);
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_Left);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 0U, 0U));
        // Copy and cut are refused; typing works as usual.
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_A);
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_C);
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_X);
        TEST_CHECK(run, (Clipboard.Text[0] == 0) && IsText(&Fixture, Id, "pass word"));
        TypeText(run, &Fixture, "secret");
        TEST_CHECK(run, IsText(&Fixture, Id, "secret"));
        // A press lands where the masks show: a mask is one character wide.
        PressAt(run, &Fixture, CharacterX(3U), LineY(0U));
        ReleaseLeft(run, &Fixture);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 3U, 3U));
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Tests: code.
static void StyleRangesMoveWithEditsAndUndo(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        uint64_t Id = AddLine(run, &Fixture, "red green");
        UITextStyleRange Ranges[2];
        TextStyle_Construct(&Ranges[0].Style);
        TextStyle_Construct(&Ranges[1].Style);
        TextStyle_SetColor(&Ranges[0].Style, UIRenderColor_FromColor(RED));
        TextStyle_SetColor(&Ranges[1].Style, UIRenderColor_FromColor(GREEN));
        TextStyle_SetFontName(&Ranges[1].Style, (const unsigned char*)u8"mono");
        Ranges[0].Start = 0U;
        Ranges[0].End = 3U;
        Ranges[1].Start = 4U;
        Ranges[1].End = 9U;
        TEST_CHECK_SUCCESS(run, UITextBox_SetStyleRanges(&Fixture.Screen, Id, Ranges, 2U));
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TEST_CHECK_SUCCESS(run, UITextBox_SetSelection(&Fixture.Screen, Id, 1U, 1U));
        TestUI_Tick(run, &Fixture);
        // Typed inside the first range it grows; typed at its end it does not.
        TypeText(run, &Fixture, "XX");
        UITextStyleRange Found;
        size_t Count = 0U;
        TEST_CHECK_SUCCESS(run, UITextBox_GetStyleRange(&Fixture.Screen, Id, 0U, &Found, &Count));
        TEST_CHECK(run, (Count == 2U) && (Found.Start == 0U) && (Found.End == 5U));
        TEST_CHECK_SUCCESS(run, UITextBox_GetStyleRange(&Fixture.Screen, Id, 1U, &Found, NULL));
        const unsigned char* FontName = NULL;
        TEST_CHECK(run, (Found.Start == 6U) && (Found.End == 11U) && TextStyle_GetFontName(&Found.Style, &FontName)
            && (strcmp((const char*)FontName, "tbp:mono") == 0));
        Tap(run, &Fixture, GameKey_Right);
        Tap(run, &Fixture, GameKey_Right);
        Tap(run, &Fixture, GameKey_Right);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 5U, 5U) || IsSelection(&Fixture, Id, 6U, 6U));
        // Undo puts the text and the ranges back.
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_Z);
        TEST_CHECK(run, IsText(&Fixture, Id, "red green"));
        TEST_CHECK_SUCCESS(run, UITextBox_GetStyleRange(&Fixture.Screen, Id, 0U, &Found, &Count));
        TEST_CHECK(run, (Count == 2U) && (Found.End == 3U));
        // Deleting a range's whole text removes it.
        TEST_CHECK_SUCCESS(run, UITextBox_SetSelection(&Fixture.Screen, Id, 0U, 4U));
        Tap(run, &Fixture, GameKey_Delete);
        TEST_CHECK_SUCCESS(run, UITextBox_GetStyleRange(&Fixture.Screen, Id, 0U, &Found, &Count));
        TEST_CHECK(run, (Count == 1U) && (Found.Start == 0U) && (Found.End == 5U));
        // Refused: a range that ends before it starts, a bad font name.
        Ranges[0].End = 0U;
        TEST_CHECK_ERROR(run, UITextBox_SetStyleRanges(&Fixture.Screen, Id, Ranges, 1U), ErrorCode_IllegalArgument);
        Ranges[0].End = 2U;
        TextStyle_SetFontName(&Ranges[0].Style, (const unsigned char*)u8"Not A Name");
        TEST_CHECK_ERROR(run, UITextBox_SetStyleRanges(&Fixture.Screen, Id, Ranges, 1U), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_SUCCESS(run, UITextBox_GetStyleRange(&Fixture.Screen, Id, 0U, &Found, &Count));
        TEST_CHECK(run, Count == 1U);
        TextStyle_Deconstruct(&Ranges[0].Style);
        TextStyle_Deconstruct(&Ranges[1].Style);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Whether a box has these ranges, in order. */
static bool IsRanges(TestUIFixture* fixture, uint64_t id, const size_t (*expected)[2], size_t count)
{
    size_t Count = 0U;
    UITextStyleRange Range;
    Error Result = UITextBox_GetStyleRange(&fixture->Screen, id, 0U, &Range, &Count);
    bool IsSame = (Result.Code == ErrorCode_Success) && (Count == count);
    Error_Deconstruct(&Result);
    for (size_t Index = 0U; IsSame && (Index < count); Index++)
    {
        Result = UITextBox_GetStyleRange(&fixture->Screen, id, Index, &Range, NULL);
        IsSame = (Result.Code == ErrorCode_Success) && (Range.Start == expected[Index][0]) && (Range.End == expected[Index][1]);
        Error_Deconstruct(&Result);
    }
    return IsSame;
}

static void RangesKeepTheirEdgesThroughEdits(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        uint64_t Id = AddLine(run, &Fixture, "0123456789");
        UITextStyleRange Ranges[2];
        TextStyle_Construct(&Ranges[0].Style);
        TextStyle_Construct(&Ranges[1].Style);
        Ranges[0].Start = 2U;
        Ranges[0].End = 5U;
        Ranges[1].Start = 5U;
        Ranges[1].End = 8U;
        TEST_CHECK_SUCCESS(run, UITextBox_SetStyleRanges(&Fixture.Screen, Id, Ranges, 2U));
        // Typed where one ends and the next starts: the first does not grow, the second moves on.
        TEST_CHECK_SUCCESS(run, UITextBox_ReplaceRange(&Fixture.Screen, Id, 5U, 5U, (const unsigned char*)"x"));
        static const size_t AfterTyping[2][2] = { { 2U, 5U }, { 6U, 9U } };
        TEST_CHECK(run, IsText(&Fixture, Id, "01234x56789") && IsRanges(&Fixture, Id, AfterTyping, 2U));
        // A selection from inside the first into the second, typed over: the first keeps its start and ends after what came in, the
        // second starts there.
        TEST_CHECK_SUCCESS(run, UITextBox_ReplaceRange(&Fixture.Screen, Id, 3U, 7U, (const unsigned char*)"Y"));
        static const size_t AfterSelection[2][2] = { { 2U, 4U }, { 4U, 6U } };
        TEST_CHECK(run, IsText(&Fixture, Id, "012Y6789") && IsRanges(&Fixture, Id, AfterSelection, 2U));
        // Text from code: what lies past its end goes, and so does what is left empty.
        TEST_CHECK_SUCCESS(run, UITextBox_SetText(&Fixture.Screen, Id, (const unsigned char*)"0123"));
        static const size_t AfterCode[1][2] = { { 2U, 4U } };
        TEST_CHECK(run, IsRanges(&Fixture, Id, AfterCode, 1U));
        TEST_CHECK_SUCCESS(run, UITextBox_SetText(&Fixture.Screen, Id, (const unsigned char*)"01"));
        TEST_CHECK(run, IsRanges(&Fixture, Id, NULL, 0U));
        TextStyle_Deconstruct(&Ranges[0].Style);
        TextStyle_Deconstruct(&Ranges[1].Style);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ComponentTreesShowWhole(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        UITextBoxOptions Options = UITextBoxOptions_Create();
        Options.IsEditable = false;
        uint64_t ReadOnly = AddBoxAt(run, &Fixture, &Options, BOX_Y, 24.0f);
        uint64_t Editable = AddLine(run, &Fixture, "");
        StringComponent* Root = NULL;
        SpriteComponent* Coin = NULL;
        TranslateComponent* Hello = NULL;
        bool IsBuilt = TEST_CHECK_SUCCESS(run, TextComponentFactory_CreateString(&Fixture.Components, (const unsigned char*)u8"a", &Root))
            && TEST_CHECK_SUCCESS(run, TextComponentFactory_CreateSprite(&Fixture.Components, (const unsigned char*)u8"coin", &Coin))
            && TEST_CHECK_SUCCESS(run, TextComponent_AddSubComponent(StringComponent_AsComponent(Root), SpriteComponent_AsComponent(Coin)))
            && TEST_CHECK_SUCCESS(run, TextComponentFactory_CreateTranslate(&Fixture.Components, (const unsigned char*)u8"greeting",
                (const unsigned char*)u8"hi", &Hello))
            && TEST_CHECK_SUCCESS(run, TextComponent_AddSubComponent(StringComponent_AsComponent(Root), TranslateComponent_AsComponent(Hello)));
        if (IsBuilt)
        {
            bool IsKeptWhole = false;
            TEST_CHECK_SUCCESS(run, UITextBox_SetComponent(&Fixture.Screen, ReadOnly, StringComponent_AsComponent(Root), &IsKeptWhole));
            TEST_CHECK(run, IsKeptWhole && IsText(&Fixture, ReadOnly, "a\xEF\xBF\xBChi"));
            TestUI_Ticks(run, &Fixture, 2U);
            // Selection over the flow's text; the sprite is left out of a copy.
            TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, ReadOnly));
            TestUI_Tick(run, &Fixture);
            Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_A);
            Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_C);
            TEST_CHECK(run, strcmp(Clipboard.Text, "ahi") == 0);
            // An editable box flattens it and says what it could not keep.
            TEST_CHECK_SUCCESS(run, UITextBox_SetComponent(&Fixture.Screen, Editable, StringComponent_AsComponent(Root), &IsKeptWhole));
            TEST_CHECK(run, !IsKeptWhole && IsText(&Fixture, Editable, "ahi"));
        }
        if (Root != NULL)
        {
            TEST_CHECK_SUCCESS(run, TextComponentFactory_ReturnTree(&Fixture.Components, StringComponent_AsComponent(Root)));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void TextFromCodeEmptiesTheUndoAndReplaceRangeIsUndoable(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    TestUIEvents Events;
    if (Construct(run, &Fixture, &Clipboard) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddLine(run, &Fixture, "start");
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TestUI_Tick(run, &Fixture);
        TypeText(run, &Fixture, "!");
        // Text set by code under the caret: the caret goes to its end and the list empties.
        TEST_CHECK_SUCCESS(run, UITextBox_SetText(&Fixture.Screen, Id, (const unsigned char*)u8"by code"));
        TEST_CHECK(run, IsSelection(&Fixture, Id, 7U, 7U));
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_Z);
        TEST_CHECK(run, IsText(&Fixture, Id, "by code"));
        // A replaced range is a step of its own.
        TEST_CHECK_SUCCESS(run, UITextBox_ReplaceRange(&Fixture.Screen, Id, 0U, 2U, (const unsigned char*)u8"set by"));
        TEST_CHECK(run, IsText(&Fixture, Id, "set by code"));
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_Z);
        TEST_CHECK(run, IsText(&Fixture, Id, "by code"));
        // The visit commits what code left too, against the text at entering.
        Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, CountEvents(&Events, Id, UIElementEvent_Committed) == 1U);
        TEST_CHECK_ERROR(run, UITextBox_ReplaceRange(&Fixture.Screen, Id, 3U, 1U, NULL), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, UITextBox_SetText(&Fixture.Screen, Id, (const unsigned char*)"\xFF"), ErrorCode_InvalidTextEncoding);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void AVisitsUndoScopeStartsEmpty(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        uint64_t Id = AddLine(run, &Fixture, "");
        UITextEditUndoOptions Undo = UITextEditUndoOptions_Create();
        TEST_CHECK_SUCCESS(run, UITextBox_SetUndoOptions(&Fixture.Screen, Id, &Undo, UITextBoxUndoScope_Visit));
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TestUI_Tick(run, &Fixture);
        TypeText(run, &Fixture, "abc");
        Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TestUI_Tick(run, &Fixture);
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_Z);
        TEST_CHECK(run, IsText(&Fixture, Id, "abc"));
        // The box's own scope keeps the list across visits.
        TEST_CHECK_SUCCESS(run, UITextBox_SetUndoOptions(&Fixture.Screen, Id, &Undo, UITextBoxUndoScope_Box));
        TypeText(run, &Fixture, "d");
        Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TestUI_Tick(run, &Fixture);
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_Z);
        TEST_CHECK(run, IsText(&Fixture, Id, "abc"));
        TEST_CHECK_ERROR(run, UITextBox_SetUndoOptions(&Fixture.Screen, Id, &Undo, (UITextBoxUndoScope)7), ErrorCode_ArgumentOutOfRange);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static Error DestroyOnChange(UIScreen* screen, const UIEvent* event, const UserData* userData)
{
    (void)userData;
    Widget* Target = NULL;
    if ((event->WidgetEventId == (int32_t)UIElementEvent_Changed) && UIScreen_TryGetWidgetById(screen, event->WidgetId, &Target))
    {
        return Widget_Destroy(Target);
    }
    return Error_CreateSuccess();
}

static void ABoxDestroyedInAHandlerGoesQuietly(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        uint64_t Id = AddLine(run, &Fixture, "");
        uint64_t Subscription = 0U;
        UserData Empty = UserData_CreateEmpty();
        TEST_CHECK_SUCCESS(run, UIScreen_Subscribe(&Fixture.Screen, Id, UI_EVENT_KIND_MASK(UIEventKind_Widget), DestroyOnChange, &Empty, &Subscription));
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TestUI_Tick(run, &Fixture);
        TypeText(run, &Fixture, "a");
        TypeText(run, &Fixture, "b");
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, !UIScreen_IsWidgetAlive(&Fixture.Screen, Id));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void LimitsRefuseWithACue(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    TestUIEvents Events;
    if (Construct(run, &Fixture, &Clipboard))
    {
        UITextBoxOptions Options = UITextBoxOptions_Create();
        Options.Limits.HasMaxCharacters = true;
        Options.Limits.MaxCharacters = 3U;
        uint64_t Id = AddBoxAt(run, &Fixture, &Options, BOX_Y, 24.0f);
        TestUI_Ticks(run, &Fixture, 2U);
        Memory_Zero(&Events, sizeof(Events));
        UserData Data = UserData_FromPointer(&Events);
        uint64_t Subscription = 0U;
        TEST_CHECK_SUCCESS(run, UIScreen_Subscribe(&Fixture.Screen, Id, UI_EVENT_KIND_MASK(UIEventKind_Cue), RecordCue, &Data, &Subscription));
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TestUI_Tick(run, &Fixture);
        TypeText(run, &Fixture, "abcd");
        TEST_CHECK(run, IsText(&Fixture, Id, "abc"));
        size_t Refused = 0U;
        for (size_t Index = 0U; Index < Events.Count; Index++)
        {
            Refused += (Events.Events[Index].WidgetEventId == (int32_t)UIElementCue_Refused) ? 1U : 0U;
        }
        TEST_CHECK(run, Refused == 1U);
        // Typing over a selection in the full box works.
        Chord(run, &Fixture, GAME_KEY_MODIFIER_SHIFT, GameKey_Left);
        TypeText(run, &Fixture, "Z");
        TEST_CHECK(run, IsText(&Fixture, Id, "abZ"));
        // New limits cut the text and empty the list.
        UITextEditLimits Limits = UITextEditLimits_Create();
        Limits.MaxBytes = 2U;
        TEST_CHECK_SUCCESS(run, UITextBox_SetLimits(&Fixture.Screen, Id, &Limits));
        TEST_CHECK(run, IsText(&Fixture, Id, "ab"));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ADisabledBoxSwallowsPressesAndKeepsNoFocus(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        UITextBoxOptions Options = UITextBoxOptions_Create();
        Options.Placeholder = (const unsigned char*)u8"type here";
        uint64_t Id = AddBoxAt(run, &Fixture, &Options, BOX_Y, 24.0f);
        TestUI_Ticks(run, &Fixture, 2U);
        UITextBoxCapability* Box = GetBox(&Fixture, Id);
        TEST_CHECK(run, (Box != NULL) && UITextBoxCapability_IsShowingPlaceholder(Box));
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UITextBox_SetIsEnabled(&Fixture.Screen, Id, false));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, !IsEntered(&Fixture, Id) && (UIScreen_GetFocusedWidgetId(&Fixture.Screen) != Id));
        PressAt(run, &Fixture, CharacterX(1U), LineY(0U));
        ReleaseLeft(run, &Fixture);
        TEST_CHECK(run, !IsEntered(&Fixture, Id));
        TEST_CHECK_SUCCESS(run, UITextBox_SetIsEnabled(&Fixture.Screen, Id, true));
        TEST_CHECK_SUCCESS(run, UITextBox_SetText(&Fixture.Screen, Id, (const unsigned char*)u8"x"));
        TEST_CHECK(run, !UITextBoxCapability_IsShowingPlaceholder(Box));
        TEST_CHECK_ERROR(run, UITextBox_SetText(&Fixture.Screen, 12345U, NULL), ErrorCode_InvalidOperation);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void TheWheelScrollsThenGoesUpTheTree(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        uint64_t Id = AddLines(run, &Fixture, "0\n1\n2\n3\n4\n5", false);
        TestUI_MovePointer(&Fixture, CharacterX(0U), LineY(1U));
        TestUI_Tick(run, &Fixture);
        TestUI_Wheel(&Fixture, -1.0f);
        TestUI_Tick(run, &Fixture);
        Vector2 Offset = { 0 };
        TEST_CHECK_SUCCESS(run, UIScrollable_GetOffset(&Fixture.Screen, Id, &Offset));
        Vector2 Range = { 0 };
        TEST_CHECK_SUCCESS(run, UIScrollable_GetRange(&Fixture.Screen, Id, &Range));
        TEST_CHECK(run, (Offset.y > 0.0f) && (Offset.y == Range.y));
        // At the end the wheel is not the box's.
        TestUI_Wheel(&Fixture, -1.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIScrollable_GetOffset(&Fixture.Screen, Id, &Offset));
        TEST_CHECK(run, Offset.y == Range.y);
        // A long line keeps its caret in view as it is typed.
        uint64_t Line = AddLine(run, &Fixture, "");
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Line));
        TestUI_Tick(run, &Fixture);
        TypeText(run, &Fixture, "0123456789012345678901234567890123456789");
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIScrollable_GetOffset(&Fixture.Screen, Line, &Offset));
        TEST_CHECK(run, Offset.x > 100.0f);
        Tap(run, &Fixture, GameKey_Home);
        TEST_CHECK_SUCCESS(run, UIScrollable_GetOffset(&Fixture.Screen, Line, &Offset));
        TEST_CHECK(run, Offset.x == 0.0f);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Whether a document's pieces are these texts, each in the colour of its tint's red channel. */
static bool IsPieces(const TextComponent* root, const char* const* texts, const unsigned char* reds, size_t count)
{
    if (TextComponent_GetSubComponentCount(root) != count)
    {
        return false;
    }
    for (size_t Index = 0U; Index < count; Index++)
    {
        const StringComponent* Piece = TextComponent_AsConstStringComponent(TextComponent_GetSubComponentAt(root, Index));
        if ((Piece == NULL) || (strcmp((const char*)StringComponent_GetText(Piece), texts[Index]) != 0)
            || (StringComponent_GetColor(Piece).Tint.r != reds[Index]))
        {
            return false;
        }
    }
    return true;
}

static void OverlappingRangesCutTheTextAndTheLaterShowsOver(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        UITextBoxOptions Options = UITextBoxOptions_Create();
        Options.Text = (const unsigned char*)"abcdefghij";
        uint64_t Id = AddBoxAt(run, &Fixture, &Options, BOX_Y, 24.0f);
        TestUI_Ticks(run, &Fixture, 2U);
        // Given out of order and overlapping: [6, 9) red 30, [1, 5) red 10, [3, 8) red 20, [3, 4) red 40.
        static const struct { size_t Start; size_t End; unsigned char Red; } Spans[] = { { 6U, 9U, 30U }, { 1U, 5U, 10U }, { 3U, 8U, 20U },
            { 3U, 4U, 40U } };
        UITextStyleRange Ranges[4];
        for (size_t Index = 0U; Index < 4U; Index++)
        {
            TextStyle_Construct(&Ranges[Index].Style);
            TextStyle_SetColor(&Ranges[Index].Style, UIRenderColor_FromColor((Color){ Spans[Index].Red, 0, 0, 255 }));
            Ranges[Index].Start = Spans[Index].Start;
            Ranges[Index].End = Spans[Index].End;
        }
        TEST_CHECK_SUCCESS(run, UITextBox_SetStyleRanges(&Fixture.Screen, Id, Ranges, 4U));
        for (size_t Index = 0U; Index < 4U; Index++)
        {
            TextStyle_Deconstruct(&Ranges[Index].Style);
        }
        UITextBoxCapability* Box = GetBox(&Fixture, Id);
        TextComponent* Root = NULL;
        if (TEST_CHECK(run, Box != NULL)
            && TEST_CHECK_SUCCESS(run, UITextBoxCapability_BuildDocument(Box, &Fixture.Components, 0x2022U, RenderColor_White(), &Root)))
        {
            // Cut at every end; on each piece the ranges over it apply in their order, so the last one given shows.
            static const char* const Texts[] = { "a", "bc", "d", "e", "f", "gh", "i", "j" };
            static const unsigned char Reds[] = { 255U, 10U, 40U, 20U, 20U, 20U, 30U, 255U };
            TEST_CHECK(run, IsPieces(Root, Texts, Reds, 8U));
            TEST_CHECK_SUCCESS(run, TextComponentFactory_ReturnTree(&Fixture.Components, Root));
        }
        // No ranges: one piece in the plain colour.
        TEST_CHECK_SUCCESS(run, UITextBox_SetStyleRanges(&Fixture.Screen, Id, NULL, 0U));
        if (TEST_CHECK(run, Box != NULL)
            && TEST_CHECK_SUCCESS(run, UITextBoxCapability_BuildDocument(Box, &Fixture.Components, 0x2022U, RenderColor_White(), &Root)))
        {
            static const char* const Whole[] = { "abcdefghij" };
            static const unsigned char White[] = { 255U };
            TEST_CHECK(run, IsPieces(Root, Whole, White, 1U));
            TEST_CHECK_SUCCESS(run, TextComponentFactory_ReturnTree(&Fixture.Components, Root));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Destroys a box (at the end of the next update). */
static void DestroyBox(TestRun* run, TestUIFixture* fixture, uint64_t id)
{
    Widget* Found = NULL;
    if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&fixture->Screen, id, &Found)))
    {
        TEST_CHECK_SUCCESS(run, Widget_Destroy(Found));
    }
    TestUI_Tick(run, fixture);
}

static void ViewsLeaveRoomForTheCaretAndTheBorder(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        // One line exactly as wide as the view (24 characters of 8 pixels in 192): the caret at its end still shows.
        uint64_t Line = AddLine(run, &Fixture, "abcdefghijklmnopqrstuvwx");
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Line));
        TEST_CHECK_SUCCESS(run, UITextBox_SetSelection(&Fixture.Screen, Line, 24U, 24U));
        TestUI_Ticks(run, &Fixture, 2U);
        Vector2 Offset = { 0 };
        TEST_CHECK_SUCCESS(run, UIScrollable_GetOffset(&Fixture.Screen, Line, &Offset));
        TEST_CHECK(run, Offset.x > 0.0f);
        TEST_CHECK_SUCCESS(run, UIScreen_LeaveWidget(&Fixture.Screen, UILeaveReason_Keep));
        DestroyBox(run, &Fixture, Line);
        // A wrapped box: lines as wide as the view never scroll across; a word longer than the view does, so its end and the caret show.
        uint64_t Wrapped = AddLines(run, &Fixture, "a few short words that wrap at the view's width, one after another", true);
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Wrapped));
        TestUI_Ticks(run, &Fixture, 2U);
        Vector2 Range = { 0 };
        TEST_CHECK_SUCCESS(run, UIScrollable_GetRange(&Fixture.Screen, Wrapped, &Range));
        TEST_CHECK(run, Range.x == 0.0f);
        TEST_CHECK_SUCCESS(run, UITextBox_SetText(&Fixture.Screen, Wrapped, (const unsigned char*)"short abcdefghijklmnopqrstuvwxyzabcdefghijklmnop end"));
        TEST_CHECK_SUCCESS(run, UITextBox_SetSelection(&Fixture.Screen, Wrapped, 48U, 48U));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK_SUCCESS(run, UIScrollable_GetRange(&Fixture.Screen, Wrapped, &Range));
        TEST_CHECK_SUCCESS(run, UIScrollable_GetOffset(&Fixture.Screen, Wrapped, &Offset));
        TEST_CHECK(run, (Range.x > 0.0f) && (Offset.x > 0.0f));
        // Trailing spaces on a line that runs on count too: the caret after them stays in view.
        TEST_CHECK_SUCCESS(run, UITextBox_SetText(&Fixture.Screen, Wrapped, (const unsigned char*)"word"));
        TEST_CHECK_SUCCESS(run, UITextBox_SetIsWrapped(&Fixture.Screen, Wrapped, false));
        TEST_CHECK_SUCCESS(run, UIScrollable_SetModes(&Fixture.Screen, Wrapped, UIScrollMode_Auto, UIScrollMode_Auto));
        TEST_CHECK_SUCCESS(run, UITextBox_SetSelection(&Fixture.Screen, Wrapped, 4U, 4U));
        TestUI_Tick(run, &Fixture);
        TypeText(run, &Fixture, "                              ");
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK_SUCCESS(run, UIScrollable_GetOffset(&Fixture.Screen, Wrapped, &Offset));
        TEST_CHECK(run, IsSelection(&Fixture, Wrapped, 34U, 34U) && (Offset.x > 0.0f));
        TEST_CHECK_SUCCESS(run, UIScreen_LeaveWidget(&Fixture.Screen, UILeaveReason_Keep));
        DestroyBox(run, &Fixture, Wrapped);
        // A border of 6 pixels around a padding of 4: the bar stands inside the border, and the text's view ends before the bar.
        UITextBoxOptions Options = UITextBoxOptions_CreateMultiline();
        Options.Text = (const unsigned char*)"a\nb";
        Options.Vertical = UIScrollMode_Always;
        Options.Element.Variant = (const unsigned char*)"outlined";
        uint64_t Bordered = AddBoxAt(run, &Fixture, &Options, BOX_Y, (4.0f * LINE) + 8.0f);
        TestUI_Ticks(run, &Fixture, 2U);
        UITextBoxCapability* Box = GetBox(&Fixture, Bordered);
        Rectangle View = (Box != NULL) ? UITextBoxCapability_GetViewBox(Box) : (Rectangle){ 0 };
        // 200 wide: 4 of padding on the left; on the right the border's 6 and the bar's 10.
        TEST_CHECK(run, (View.x == 4.0f) && (View.width == (BOX_WIDTH - 4.0f - 6.0f - 10.0f)));
        Rectangle Bar = TestUI_GetPixelBox(run, &Fixture, (Box != NULL) ? Box->_barIds[UIScrollAxis_Vertical] : WIDGET_ID_INVALID);
        TEST_CHECK(run, TestUI_IsSamePixel(Bar.x + Bar.width, BOX_X + BOX_WIDTH - 6.0f) && TestUI_IsSamePixel(Bar.y, BOX_Y + 6.0f));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ShiftTabLeavesABoxThatTypesTabs(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        uint64_t Id = AddLines(run, &Fixture, "", false);
        TEST_CHECK_SUCCESS(run, UITextBox_SetKeys(&Fixture.Screen, Id, true, true, false));
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TestUI_Tick(run, &Fixture);
        Tap(run, &Fixture, GameKey_Tab);
        TEST_CHECK(run, IsText(&Fixture, Id, "\t") && IsEntered(&Fixture, Id));
        // Shift+Tab still leaves, backwards (here back into the only box), and types nothing.
        Chord(run, &Fixture, GAME_KEY_MODIFIER_SHIFT, GameKey_Tab);
        TEST_CHECK(run, IsText(&Fixture, Id, "\t"));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ABoxWithoutAThemeLaysOutAndTypes(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        // No theme: the box draws as plain boxes, and still lays its text out, scrolls and types.
        uint64_t Lines = AddLines(run, &Fixture, "0\n1\n2\n3\n4\n5", false);
        uint64_t Line = AddLine(run, &Fixture, "plain");
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Line));
        TestUI_Tick(run, &Fixture);
        Tap(run, &Fixture, GameKey_End);
        TypeText(run, &Fixture, "!");
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsText(&Fixture, Line, "plain!"));
        // With no theme there is no font here (raylib's own needs a window), so the glyphs have no width: the lines still stack.
        Vector2 Range = { 0 };
        TEST_CHECK_SUCCESS(run, UIScrollable_GetRange(&Fixture.Screen, Lines, &Range));
        TEST_CHECK(run, Range.y > 0.0f);
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Tests: what an independent review found.
static size_t CountRefused(const TestUIEvents* events)
{
    size_t Count = 0U;
    for (size_t Index = 0U; Index < events->Count; Index++)
    {
        Count += (events->Events[Index].WidgetEventId == (int32_t)UIElementCue_Refused) ? 1U : 0U;
    }
    return Count;
}

static bool IsRefusal(Error result)
{
    bool IsRefused = result.Code != ErrorCode_Success;
    Error_Deconstruct(&result);
    return IsRefused;
}

static void AStyleAComponentRefusesIsRefusedWhenSet(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        uint64_t Id = AddLine(run, &Fixture, "styled text");
        UITextStyleRange Range;
        TextStyle_Construct(&Range.Style);
        Range.Start = 0U;
        Range.End = 4U;
        TextStyle_SetStringSize(&Range.Style, -1.0f);
        TEST_CHECK(run, IsRefusal(UITextBox_SetStyleRanges(&Fixture.Screen, Id, &Range, 1U)));
        TextStyle_Construct(&Range.Style);
        TextStyle_SetSpacing(&Range.Style, 1.0e9f);
        TEST_CHECK(run, IsRefusal(UITextBox_SetStyleRanges(&Fixture.Screen, Id, &Range, 1U)));
        // Nothing was set, and the box goes on being laid out and drawn (each tick checks the update).
        TEST_CHECK(run, IsRanges(&Fixture, Id, NULL, 0U));
        TestUI_Ticks(run, &Fixture, 2U);
        TextStyle_Construct(&Range.Style);
        TextStyle_SetStringSize(&Range.Style, 2.0f);
        TEST_CHECK_SUCCESS(run, UITextBox_SetStyleRanges(&Fixture.Screen, Id, &Range, 1U));
        TestUI_Ticks(run, &Fixture, 2U);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void AltGrTypesWhatItTypes(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        GameInputBinding Q = { .Key = GameKey_Q, .Modifiers = GAME_KEY_MODIFIER_NONE };
        TEST_CHECK_SUCCESS(run, GameInput_SetBindings(&Fixture.Input, GameAction_UIBack, &Q, 1U));
        uint64_t Id = AddLine(run, &Fixture, "");
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TestUI_Tick(run, &Fixture);
        TypeText(run, &Fixture, "hello");
        // Ctrl with Alt types (AltGr on Windows): 'ą' from A, never select all then the letter over everything.
        HoldModifiers(&Fixture, GAME_KEY_MODIFIER_CONTROL | GAME_KEY_MODIFIER_ALT, true);
        TestUI_Press(&Fixture, GameKey_A);
        Fixture.Sample.TextCodepoints[0] = 0x0105U;
        Fixture.Sample.TextCodepointCount = 1U;
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_A);
        HoldModifiers(&Fixture, GAME_KEY_MODIFIER_CONTROL | GAME_KEY_MODIFIER_ALT, false);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsText(&Fixture, Id, "hello\xC4\x85"));
        // The right Alt alone is AltGr as GLFW reports it: Q, bound to ui_back, types '@', and the box stays entered.
        TestUI_Press(&Fixture, GameKey_RightAlt);
        TestUI_Tick(run, &Fixture);
        TestUI_Press(&Fixture, GameKey_Q);
        Fixture.Sample.TextCodepoints[0] = (uint32_t)'@';
        Fixture.Sample.TextCodepointCount = 1U;
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, !GameInput_IsKeyPressed(&Fixture.Input, GameKey_Q));
        TestUI_Release(&Fixture, GameKey_Q);
        TestUI_Release(&Fixture, GameKey_RightAlt);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsText(&Fixture, Id, "hello\xC4\x85@") && IsEntered(&Fixture, Id));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void AnEmptyBoxHasACaretALineTall(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        uint64_t Id = AddLine(run, &Fixture, "");
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TestUI_Ticks(run, &Fixture, 2U);
        UITextBoxCapability* Box = GetBox(&Fixture, Id);
        Widget* Found = NULL;
        const TextLayout* Layout = NULL;
        float UnitsToUI = 0.0f;
        Rectangle Caret = { 0 };
        bool IsLaidOut = TEST_CHECK(run, (Box != NULL) && UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Found))
            && TEST_CHECK_SUCCESS(run, Box->_geometry->LayOut(Found, Box->_isWrapped, Box->_wrapWidth, &Layout, &UnitsToUI))
            && TEST_CHECK(run, (Layout != NULL) && TextLayout_GetCaretBox(Layout, 0U, &Caret));
        TEST_CHECK(run, IsLaidOut && TestUI_IsSamePixel(Caret.height * UnitsToUI, LINE));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A right click at a place of the box, its menu then open. */
static void RightClickAt(TestRun* run, TestUIFixture* fixture, float x, float y)
{
    TestUI_MovePointer(fixture, x, y);
    TestUI_Tick(run, fixture);
    TestUI_Press(fixture, GameKey_MouseRight);
    TestUI_Tick(run, fixture);
    TestUI_Release(fixture, GameKey_MouseRight);
    TestUI_Ticks(run, fixture, 2U);
}

static void TheBoxsOwnMenuKeepsItsVisit(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    TestUIEvents Events;
    if (Construct(run, &Fixture, &Clipboard) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        // A field whose undo is the visit's: the menu's Undo takes back what was typed before it opened.
        uint64_t Id = AddLine(run, &Fixture, "");
        UITextEditUndoOptions Undo = UITextEditUndoOptions_Create();
        TEST_CHECK_SUCCESS(run, UITextBox_SetUndoOptions(&Fixture.Screen, Id, &Undo, UITextBoxUndoScope_Visit));
        PressAt(run, &Fixture, CharacterX(0U), LineY(0U));
        ReleaseLeft(run, &Fixture);
        TypeText(run, &Fixture, "abc");
        RightClickAt(run, &Fixture, CharacterX(1U), LineY(0U));
        TEST_CHECK(run, (CountPopups(&Fixture) == 1U) && (CountEvents(&Events, Id, UIElementEvent_Committed) == 0U));
        Tap(run, &Fixture, GameKey_Down);
        Tap(run, &Fixture, GameKey_Enter);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsText(&Fixture, Id, "") && IsEntered(&Fixture, Id) && (CountPopups(&Fixture) == 0U));
        // Escape then reverts to the text the visit began with: here nothing changed, so nothing is told.
        Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, !IsEntered(&Fixture, Id) && (CountEvents(&Events, Id, UIElementEvent_Committed) == 0U)
            && (CountEvents(&Events, Id, UIElementEvent_Cancelled) == 0U));
        // A menu closed by Escape gives the visit back, and the next Escape reverts all of it.
        TEST_CHECK_SUCCESS(run, UITextBox_SetText(&Fixture.Screen, Id, (const unsigned char*)"name"));
        PressAt(run, &Fixture, CharacterX(4U), LineY(0U));
        ReleaseLeft(run, &Fixture);
        TypeText(run, &Fixture, "abc");
        RightClickAt(run, &Fixture, CharacterX(2U), LineY(0U));
        Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, (CountPopups(&Fixture) == 0U) && IsEntered(&Fixture, Id));
        Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, IsText(&Fixture, Id, "name") && (CountEvents(&Events, Id, UIElementEvent_Cancelled) == 1U)
            && (CountEvents(&Events, Id, UIElementEvent_Committed) == 0U));
        // A menu closed by a press elsewhere ends the visit there, kept.
        PressAt(run, &Fixture, CharacterX(4U), LineY(0U));
        ReleaseLeft(run, &Fixture);
        TypeText(run, &Fixture, "x");
        RightClickAt(run, &Fixture, CharacterX(2U), LineY(0U));
        PressAt(run, &Fixture, 900.0f, 450.0f);
        ReleaseLeft(run, &Fixture);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, !IsEntered(&Fixture, Id) && IsText(&Fixture, Id, "namex") && (CountEvents(&Events, Id, UIElementEvent_Committed) == 1U));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ARevertThatCannotStepBackKeepsTheRanges(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        uint64_t Id = AddLine(run, &Fixture, "hello world");
        UITextStyleRange Ranges[2];
        TextStyle_Construct(&Ranges[0].Style);
        TextStyle_Construct(&Ranges[1].Style);
        Ranges[0].Start = 0U;
        Ranges[0].End = 5U;
        Ranges[1].Start = 6U;
        Ranges[1].End = 11U;
        TEST_CHECK_SUCCESS(run, UITextBox_SetStyleRanges(&Fixture.Screen, Id, Ranges, 2U));
        // A first visit leaves a step behind.
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TEST_CHECK_SUCCESS(run, UITextBox_SetSelection(&Fixture.Screen, Id, 11U, 11U));
        TestUI_Tick(run, &Fixture);
        TypeText(run, &Fixture, "!");
        Tap(run, &Fixture, GameKey_Enter);
        // The second undoes past where it began, then types: its mark is gone, so Escape sets the text back whole.
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TestUI_Tick(run, &Fixture);
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_Z);
        TypeText(run, &Fixture, "?");
        Tap(run, &Fixture, GameKey_Escape);
        static const size_t Kept[2][2] = { { 0U, 5U }, { 6U, 11U } };
        TEST_CHECK(run, IsText(&Fixture, Id, "hello world!") && IsRanges(&Fixture, Id, Kept, 2U));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ABoxShowingATreeKeepsItWhole(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        UITextBoxOptions Options = UITextBoxOptions_Create();
        Options.IsEditable = false;
        uint64_t Id = AddBoxAt(run, &Fixture, &Options, BOX_Y, 24.0f);
        StringComponent* Root = NULL;
        SpriteComponent* Coin = NULL;
        bool IsBuilt = TEST_CHECK_SUCCESS(run, TextComponentFactory_CreateString(&Fixture.Components, (const unsigned char*)u8"abc", &Root))
            && TEST_CHECK_SUCCESS(run, TextComponentFactory_CreateSprite(&Fixture.Components, (const unsigned char*)u8"coin", &Coin))
            && TEST_CHECK_SUCCESS(run, TextComponent_AddSubComponent(StringComponent_AsComponent(Root), SpriteComponent_AsComponent(Coin)))
            && TEST_CHECK_SUCCESS(run, UITextBox_SetComponent(&Fixture.Screen, Id, StringComponent_AsComponent(Root), NULL));
        if (IsBuilt)
        {
            TestUI_Ticks(run, &Fixture, 2U);
            // Code changes it by a tree or a text, never a range of what it shows; new limits leave what it shows whole.
            TEST_CHECK(run, IsRefusal(UITextBox_ReplaceRange(&Fixture.Screen, Id, 0U, 1U, (const unsigned char*)"X")));
            UITextEditLimits Limits = UITextEditLimits_Create();
            Limits.MaxBytes = 2U;
            TEST_CHECK_SUCCESS(run, UITextBox_SetLimits(&Fixture.Screen, Id, &Limits));
            TEST_CHECK(run, IsText(&Fixture, Id, "abc\xEF\xBF\xBC"));
            // Masked, it shows masks, the sprite one of them.
            TEST_CHECK_SUCCESS(run, UITextBox_SetIsMasked(&Fixture.Screen, Id, true));
            UITextBoxCapability* Box = GetBox(&Fixture, Id);
            TextComponent* Shown = NULL;
            if (TEST_CHECK(run, Box != NULL)
                && TEST_CHECK_SUCCESS(run, UITextBoxCapability_BuildDocument(Box, &Fixture.Components, (uint32_t)'*', RenderColor_White(), &Shown)))
            {
                const StringComponent* Masks = TextComponent_AsConstStringComponent(TextComponent_GetSubComponentAt(Shown, 0U));
                TEST_CHECK(run, (Masks != NULL) && (strcmp((const char*)StringComponent_GetText(Masks), "****") == 0));
                TEST_CHECK_SUCCESS(run, TextComponentFactory_ReturnTree(&Fixture.Components, Shown));
            }
            TEST_CHECK_SUCCESS(run, UITextBox_SetIsMasked(&Fixture.Screen, Id, false));
            // Made editable, it keeps the tree's text without the sprite, through its limits.
            Limits = UITextEditLimits_Create();
            TEST_CHECK_SUCCESS(run, UITextBox_SetLimits(&Fixture.Screen, Id, &Limits));
            TEST_CHECK_SUCCESS(run, UITextBox_SetIsEditable(&Fixture.Screen, Id, true));
            TEST_CHECK(run, IsText(&Fixture, Id, "abc"));
            TEST_CHECK_SUCCESS(run, UITextBox_ReplaceRange(&Fixture.Screen, Id, 0U, 1U, (const unsigned char*)"X"));
            TEST_CHECK(run, IsText(&Fixture, Id, "Xbc"));
        }
        if (Root != NULL)
        {
            TEST_CHECK_SUCCESS(run, TextComponentFactory_ReturnTree(&Fixture.Components, StringComponent_AsComponent(Root)));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void EnterAtTheLineLimitIsRefused(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    TestUIEvents Events;
    if (Construct(run, &Fixture, &Clipboard))
    {
        UITextBoxOptions Options = UITextBoxOptions_CreateMultiline();
        Options.Text = (const unsigned char*)"a\nb";
        Options.Limits.HasMaxLines = true;
        Options.Limits.MaxLines = 2U;
        uint64_t Id = AddBoxAt(run, &Fixture, &Options, BOX_Y, (4.0f * LINE) + 8.0f);
        TestUI_Ticks(run, &Fixture, 2U);
        Memory_Zero(&Events, sizeof(Events));
        UserData Data = UserData_FromPointer(&Events);
        uint64_t Subscription = 0U;
        TEST_CHECK_SUCCESS(run, UIScreen_Subscribe(&Fixture.Screen, Id, UI_EVENT_KIND_MASK(UIEventKind_Cue), RecordCue, &Data, &Subscription));
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TEST_CHECK_SUCCESS(run, UITextBox_SetSelection(&Fixture.Screen, Id, 3U, 3U));
        TestUI_Tick(run, &Fixture);
        Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, IsText(&Fixture, Id, "a\nb") && IsEntered(&Fixture, Id) && (CountRefused(&Events) == 1U));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A filter that tries to set its own box's text: the box refuses it while the filter runs. */
typedef struct MeddlingFilterStruct
{
    UIScreen* Screen;
    uint64_t Id;
    ErrorCode LastCode;
} MeddlingFilter;

static bool MeddlingFilter_Keep(uint32_t codepoint, const UserData* userData)
{
    (void)codepoint;
    MeddlingFilter* Filter = UserData_GetPointer(userData);
    Error Result = UITextBox_SetText(Filter->Screen, Filter->Id, (const unsigned char*)"meddled");
    Filter->LastCode = Result.Code;
    Error_Deconstruct(&Result);
    return true;
}

static void AFilterCannotChangeItsBox(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        MeddlingFilter Filter = { .Screen = &Fixture.Screen, .Id = WIDGET_ID_INVALID, .LastCode = ErrorCode_Success };
        UITextBoxOptions Options = UITextBoxOptions_Create();
        Options.Limits.Filter = MeddlingFilter_Keep;
        Options.Limits.FilterData = UserData_FromPointer(&Filter);
        Filter.Id = AddBoxAt(run, &Fixture, &Options, BOX_Y, 24.0f);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Filter.Id));
        TestUI_Tick(run, &Fixture);
        TypeText(run, &Fixture, "ok");
        TEST_CHECK(run, IsText(&Fixture, Filter.Id, "ok") && (Filter.LastCode == ErrorCode_InvalidOperation));
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_Z);
        TEST_CHECK(run, IsText(&Fixture, Filter.Id, ""));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void UpAndDownMeasureAgainAfterAnEdit(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        uint64_t Id = AddLines(run, &Fixture, "0123456789\n01\n0123456789", false);
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TEST_CHECK_SUCCESS(run, UITextBox_SetSelection(&Fixture.Screen, Id, 9U, 9U));
        TestUI_Tick(run, &Fixture);
        Tap(run, &Fixture, GameKey_Down);
        TEST_CHECK(run, IsSelection(&Fixture, Id, 13U, 13U));
        // A paste moves the caret: Down goes on from where it now stands, not the column remembered before.
        snprintf(Clipboard.Text, sizeof(Clipboard.Text), "XYZ");
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_V);
        Tap(run, &Fixture, GameKey_Down);
        TEST_CHECK(run, IsText(&Fixture, Id, "0123456789\n01XYZ\n0123456789") && IsSelection(&Fixture, Id, 22U, 22U));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void AWrappedBoxWithoutBreaksMovesBetweenItsLines(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        UITextBoxOptions Options = UITextBoxOptions_CreateMultiline();
        Options.Limits.AllowsNewlines = false;
        Options.Text = (const unsigned char*)"aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii jjjj";
        uint64_t Id = AddBoxAt(run, &Fixture, &Options, BOX_Y, (4.0f * LINE) + 8.0f);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TEST_CHECK_SUCCESS(run, UITextBox_SetSelection(&Fixture.Screen, Id, 2U, 2U));
        TestUI_Tick(run, &Fixture);
        Tap(run, &Fixture, GameKey_Down);
        size_t Caret = 0U;
        TEST_CHECK_SUCCESS(run, UITextBox_GetSelection(&Fixture.Screen, Id, NULL, &Caret));
        TEST_CHECK(run, (Caret > 20U) && IsEntered(&Fixture, Id));
        // Enter still leaves: no line break can go in.
        Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, !IsEntered(&Fixture, Id));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void APasteReadsWhatTheBoxCanTake(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    if (Construct(run, &Fixture, &Clipboard))
    {
        UITextBoxOptions Options = UITextBoxOptions_Create();
        Options.Limits.MaxBytes = 100000U;
        uint64_t Id = AddBoxAt(run, &Fixture, &Options, BOX_Y, 24.0f);
        TestUI_Ticks(run, &Fixture, 2U);
        size_t LargeLength = 300000U;
        char* Large = Memory_Allocate(LargeLength + 1U);
        Memory_Set(Large, 'a', LargeLength);
        Large[LargeLength] = 0;
        Clipboard.Large = Large;
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TestUI_Tick(run, &Fixture);
        Chord(run, &Fixture, GAME_KEY_MODIFIER_CONTROL, GameKey_V);
        // It asks for what its limits could take, takes what fits, and keeps no room for the rest.
        UITextBoxCapability* Box = GetBox(&Fixture, Id);
        size_t Length = 0U;
        const unsigned char* Text = NULL;
        TEST_CHECK_SUCCESS(run, UITextBox_GetText(&Fixture.Screen, Id, &Text, &Length));
        TEST_CHECK(run, (Clipboard.LastMaxBytes == ((100000U * 4U) + 16U)) && (Length == 100000U));
        TEST_CHECK(run, (Box != NULL) && (Box->_scratch._capacity < LargeLength));
        Clipboard.Large = NULL;
        Memory_Free(Large);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void EscapeAfterAnUnchangedVisitTellsNothing(TestRun* run)
{
    TestUIFixture Fixture;
    TestClipboard Clipboard;
    TestUIEvents Events;
    if (Construct(run, &Fixture, &Clipboard) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddLine(run, &Fixture, "same");
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
        TEST_CHECK_SUCCESS(run, UITextBox_SetSelection(&Fixture.Screen, Id, 4U, 4U));
        TestUI_Tick(run, &Fixture);
        TypeText(run, &Fixture, "x");
        Tap(run, &Fixture, GameKey_Backspace);
        Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, IsText(&Fixture, Id, "same") && !IsEntered(&Fixture, Id) && (CountEvents(&Events, Id, UIElementEvent_Cancelled) == 0U)
            && (CountEvents(&Events, Id, UIElementEvent_Committed) == 0U));
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Public functions.
void UITextBoxTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "typing_on_a_focused_box_does_nothing", TypingOnAFocusedBoxDoesNothing },
        { "enter_selects_a_one_line_box_and_typing_replaces_it", EnterSelectsAOneLineBoxAndTypingReplacesIt },
        { "escape_reverts_one_line_and_keeps_several", EscapeRevertsOneLineAndKeepsSeveral },
        { "a_press_places_the_caret_and_code_leaves_it", APressPlacesTheCaretAndCodeLeavesIt },
        { "tab_carries_on_into_the_next_box_selected", TabCarriesOnIntoTheNextBoxSelected },
        { "the_arrows_home_and_end_move_the_caret", TheArrowsHomeAndEndMoveTheCaret },
        { "backspace_and_delete_by_character_and_word", BackspaceAndDeleteByCharacterAndWord },
        { "insert_turns_overwrite_on_until_the_box_is_left", InsertTurnsOverwriteOnUntilTheBoxIsLeft },
        { "typing_groups_into_undo_steps", TypingGroupsIntoUndoSteps },
        { "the_clipboard_keys_copy_cut_and_paste", TheClipboardKeysCopyCutAndPaste },
        { "up_and_down_keep_the_column_across_short_lines", UpAndDownKeepTheColumnAcrossShortLines },
        { "home_and_end_keep_to_the_wrapped_line", HomeAndEndKeepToTheWrappedLine },
        { "page_keys_move_by_the_view", PageKeysMoveByTheView },
        { "what_a_box_leaves_reaches_its_owner", WhatABoxLeavesReachesItsOwner },
        { "a_key_held_past_leaving_does_nothing_more", AKeyHeldPastLeavingDoesNothingMore },
        { "a_letter_bound_to_back_types_the_letter", ALetterBoundToBackTypesTheLetter },
        { "a_drag_selects_and_shift_extends", ADragSelectsAndShiftExtends },
        { "double_and_triple_clicks_select_words_and_lines", DoubleAndTripleClicksSelectWordsAndLines },
        { "a_drag_past_an_edge_scrolls", ADragPastAnEdgeScrolls },
        { "a_right_click_opens_the_menu", ARightClickOpensTheMenu },
        { "the_short_list_copies_pastes_and_resets_the_whole_text", TheShortListCopiesPastesAndResetsTheWholeText },
        { "a_read_only_box_selects_copies_and_scrolls", AReadOnlyBoxSelectsCopiesAndScrolls },
        { "a_masked_box_hides_its_text_and_is_one_word", AMaskedBoxHidesItsTextAndIsOneWord },
        { "style_ranges_move_with_edits_and_undo", StyleRangesMoveWithEditsAndUndo },
        { "ranges_keep_their_edges_through_edits", RangesKeepTheirEdgesThroughEdits },
        { "component_trees_show_whole", ComponentTreesShowWhole },
        { "text_from_code_empties_the_undo_and_replace_range_is_undoable", TextFromCodeEmptiesTheUndoAndReplaceRangeIsUndoable },
        { "a_visits_undo_scope_starts_empty", AVisitsUndoScopeStartsEmpty },
        { "a_box_destroyed_in_a_handler_goes_quietly", ABoxDestroyedInAHandlerGoesQuietly },
        { "limits_refuse_with_a_cue", LimitsRefuseWithACue },
        { "a_disabled_box_swallows_presses_and_keeps_no_focus", ADisabledBoxSwallowsPressesAndKeepsNoFocus },
        { "the_wheel_scrolls_then_goes_up_the_tree", TheWheelScrollsThenGoesUpTheTree },
        { "overlapping_ranges_cut_the_text_and_the_later_shows_over", OverlappingRangesCutTheTextAndTheLaterShowsOver },
        { "views_leave_room_for_the_caret_and_the_border", ViewsLeaveRoomForTheCaretAndTheBorder },
        { "shift_tab_leaves_a_box_that_types_tabs", ShiftTabLeavesABoxThatTypesTabs },
        { "a_box_without_a_theme_lays_out_and_types", ABoxWithoutAThemeLaysOutAndTypes },
        { "a_style_a_component_refuses_is_refused_when_set", AStyleAComponentRefusesIsRefusedWhenSet },
        { "altgr_types_what_it_types", AltGrTypesWhatItTypes },
        { "an_empty_box_has_a_caret_a_line_tall", AnEmptyBoxHasACaretALineTall },
        { "the_boxs_own_menu_keeps_its_visit", TheBoxsOwnMenuKeepsItsVisit },
        { "a_revert_that_cannot_step_back_keeps_the_ranges", ARevertThatCannotStepBackKeepsTheRanges },
        { "a_box_showing_a_tree_keeps_it_whole", ABoxShowingATreeKeepsItWhole },
        { "enter_at_the_line_limit_is_refused", EnterAtTheLineLimitIsRefused },
        { "a_filter_cannot_change_its_box", AFilterCannotChangeItsBox },
        { "up_and_down_measure_again_after_an_edit", UpAndDownMeasureAgainAfterAnEdit },
        { "a_wrapped_box_without_breaks_moves_between_its_lines", AWrappedBoxWithoutBreaksMovesBetweenItsLines },
        { "a_paste_reads_what_the_box_can_take", APasteReadsWhatTheBoxCanTake },
        { "escape_after_an_unchanged_visit_tells_nothing", EscapeAfterAnUnchangedVisitTellsNothing },
    };
    TestRun_RunSuite(run, "UITextBox", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
