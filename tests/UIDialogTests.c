#include "TestSuites.h"
#include "TestUI.h"
#include "UIButton.h"
#include "UIDialog.h"
#include "UIKit.h"
#include "UIPopup.h"
#include "UIWidgetFactory.h"
#include "wr/WRUserData.h"
#include <string.h>


// Macros.
/** The fixture's look: a dialog over a backdrop, with buttons, labels, a text box and a property box. */
#define DIALOG_THEME \
    "{\"name\": \"dialogs\", \"font\": \"mono\", \"text_size\": 16, \"elements\": {" \
    "\"panel\": {\"padding\": [0, 0], \"spacing\": 0, \"background\": {\"kind\": \"flat\"}}," \
    "\"button\": {\"padding\": [8, 4], \"min_size\": [24, 24], \"background\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#e0e0e0\"}}," \
    "\"label\": {\"text\": {\"normal\": \"#e0e0e0\"}}," \
    "\"backdrop\": {\"background\": {\"kind\": \"flat\", \"colors\": {\"normal\": \"#00000080\"}}}," \
    "\"scrollbar\": {\"thickness\": 10, \"min_thumb_length\": 10, \"thumb_inset\": 0, \"track\": {\"kind\": \"flat\"}, \"thumb\": {\"kind\": \"flat\"}}," \
    "\"dialog\": {\"padding\": [10, 10], \"spacing\": 8, \"button_spacing\": 6, \"button_width\": 70, \"min_size\": [200, 0], \"max_width\": 400," \
    "\"background\": {\"kind\": \"flat\"}}}}"
#define ANSWER_OK 1
#define ANSWER_CANCEL 2
#define ANSWER_LATER 3


// Types.
/* What a dialog's answer handler saw: the answer, and the fields read by id at that moment. */
typedef struct AnswerProbeStruct
{
    size_t AnswerCount;
    int32_t Answer;
    uint64_t TextBoxId;
    unsigned char Text[64];
    uint64_t ValueBoxId;
    double Value;
    size_t FieldEventsBefore;
    size_t FieldEvents;
} AnswerProbe;


// Static functions: helpers.
static bool Construct(TestRun* run, TestUIFixture* fixture)
{
    return TestUI_Construct(run, fixture) && TestUI_SetTheme(run, fixture, DIALOG_THEME);
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

static Error OnAnswer(UIScreen* screen, const UIEvent* event, const UserData* userData)
{
    AnswerProbe* Probe = UserData_GetPointer(userData);
    if ((event->Kind != UIEventKind_Widget) || (event->WidgetEventId != (int32_t)UIElementEvent_Activated) || !event->HasIndex)
    {
        return Error_CreateSuccess();
    }
    Probe->AnswerCount++;
    Probe->Answer = event->Index;
    Probe->FieldEventsBefore = Probe->FieldEvents;
    const unsigned char* Text = NULL;
    size_t Length = 0U;
    Probe->Text[0] = 0U;
    if (Probe->TextBoxId != WIDGET_ID_INVALID)
    {
        Error Result = UITextBox_GetText(screen, Probe->TextBoxId, &Text, &Length);
        if ((Result.Code == ErrorCode_Success) && (Length < sizeof(Probe->Text)))
        {
            memcpy(Probe->Text, Text, Length);
            Probe->Text[Length] = 0U;
        }
        Error_Deconstruct(&Result);
    }
    if (Probe->ValueBoxId != WIDGET_ID_INVALID)
    {
        Error Result = UIValue_Get(screen, Probe->ValueBoxId, &Probe->Value);
        Error_Deconstruct(&Result);
    }
    return Error_CreateSuccess();
}

/* Counts the fields' own value events, to tell they came before the answer. */
static Error OnFieldEvent(UIScreen* screen, const UIEvent* event, const UserData* userData)
{
    (void)screen;
    AnswerProbe* Probe = UserData_GetPointer(userData);
    if ((event->WidgetEventId == (int32_t)UIElementEvent_Committed) || (event->WidgetEventId == (int32_t)UIElementEvent_Cancelled))
    {
        Probe->FieldEvents++;
    }
    return Error_CreateSuccess();
}

static const UIDialogButton OkCancel[] =
{
    { .Id = ANSWER_OK, .Text = u8"OK", .IsDefault = true, .IsCancel = false },
    { .Id = ANSWER_LATER, .Text = u8"Later", .IsDefault = false, .IsCancel = false },
    { .Id = ANSWER_CANCEL, .Text = u8"Cancel", .IsDefault = false, .IsCancel = true },
};

static uint64_t MakeDialog(TestRun* run, TestUIFixture* fixture, const UIDialogButton* buttons, size_t count, uint64_t ownerId, AnswerProbe* probe)
{
    UIDialogOptions Options = UIDialogOptions_Create(u8"Unsaved changes");
    Options.Message = u8"Save the changes before closing?";
    Options.Buttons = buttons;
    Options.ButtonCount = count;
    Options.OwnerId = ownerId;
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIKit_CreateDialog(&fixture->Kit, &fixture->Screen, &Options, &Id));
    UserData Data = UserData_FromPointer(probe);
    uint64_t Subscription = UI_SCREEN_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIScreen_Subscribe(&fixture->Screen, Id, UI_EVENT_KIND_MASK(UIEventKind_Widget), OnAnswer, &Data, &Subscription));
    return Id;
}

static uint64_t AddTextField(TestRun* run, TestUIFixture* fixture, uint64_t dialogId, AnswerProbe* probe)
{
    uint64_t ContentId = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIDialog_GetContentId(&fixture->Screen, dialogId, &ContentId));
    UITextBoxOptions Box = UITextBoxOptions_Create();
    Box.Element.Hint = UIStackHint_Fixed(24.0f);
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIKit_CreateTextBox(&fixture->Kit, &fixture->Screen, ContentId, &Box, &Id));
    UserData Data = UserData_FromPointer(probe);
    uint64_t Subscription = UI_SCREEN_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIScreen_Subscribe(&fixture->Screen, Id, UI_EVENT_KIND_MASK(UIEventKind_Widget), OnFieldEvent, &Data, &Subscription));
    probe->TextBoxId = Id;
    return Id;
}

static bool IsOpen(TestUIFixture* fixture, uint64_t dialogId)
{
    return UIScreen_IsPopupOpen(&fixture->Screen, dialogId);
}


// Tests.
static void EnterAnswersFromAnEnteredFieldInOnePress(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        AnswerProbe Probe = { .TextBoxId = WIDGET_ID_INVALID, .ValueBoxId = WIDGET_ID_INVALID };
        uint64_t Id = MakeDialog(run, &Fixture, OkCancel, 3U, WIDGET_ID_INVALID, &Probe);
        uint64_t Field = AddTextField(run, &Fixture, Id, &Probe);
        TEST_CHECK_SUCCESS(run, UIDialog_Open(&Fixture.Screen, Id, Field));
        TestUI_Ticks(run, &Fixture, 3U);
        // A dialog that opens with a field enters it: typing works at once.
        TEST_CHECK(run, (UIScreen_GetEnteredWidgetId(&Fixture.Screen) == Field) && IsOpen(&Fixture, Id));
        TypeText(run, &Fixture, "town");
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        // One Enter: the field left keeping its text (its Committed first), then the default answer, read while the field still lives.
        TEST_CHECK(run, (Probe.AnswerCount == 1U) && (Probe.Answer == ANSWER_OK));
        TEST_CHECK(run, strcmp((const char*)Probe.Text, "town") == 0);
        TEST_CHECK(run, Probe.FieldEventsBefore == 1U);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, !IsOpen(&Fixture, Id) && !UIScreen_IsWidgetAlive(&Fixture.Screen, Field));
        TEST_CHECK(run, Probe.AnswerCount == 1U);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void EscapeCancelsFromAnEnteredFieldInOnePress(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        AnswerProbe Probe = { .TextBoxId = WIDGET_ID_INVALID, .ValueBoxId = WIDGET_ID_INVALID };
        uint64_t Id = MakeDialog(run, &Fixture, OkCancel, 3U, WIDGET_ID_INVALID, &Probe);
        uint64_t Field = AddTextField(run, &Fixture, Id, &Probe);
        TEST_CHECK_SUCCESS(run, UIDialog_Open(&Fixture.Screen, Id, Field));
        TestUI_Ticks(run, &Fixture, 3U);
        TypeText(run, &Fixture, "town");
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        // The field cancelled (a one-line box goes back to its text at entering), then the cancel answer; the game never sees the Escape.
        TEST_CHECK(run, (Probe.AnswerCount == 1U) && (Probe.Answer == ANSWER_CANCEL) && (Probe.Text[0] == 0U));
        TEST_CHECK(run, !GameInput_IsActionPressed(&Fixture.Input, GameAction_Pause));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, !IsOpen(&Fixture, Id));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void EnterOnAFieldOnlyFocusedEntersIt(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        AnswerProbe Probe = { .TextBoxId = WIDGET_ID_INVALID, .ValueBoxId = WIDGET_ID_INVALID };
        uint64_t Id = MakeDialog(run, &Fixture, OkCancel, 3U, WIDGET_ID_INVALID, &Probe);
        uint64_t Field = AddTextField(run, &Fixture, Id, &Probe);
        TEST_CHECK_SUCCESS(run, UIDialog_Open(&Fixture.Screen, Id, WIDGET_ID_INVALID));
        TestUI_Ticks(run, &Fixture, 3U);
        Widget* FieldWidget = NULL;
        TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Field, &FieldWidget));
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, FieldWidget));
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.Screen) != Field);
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, (UIScreen_GetEnteredWidgetId(&Fixture.Screen) == Field) && (Probe.AnswerCount == 0U) && IsOpen(&Fixture, Id));
        // Escape on the field only focused (it leaves the field first, then is it): the cancel answer.
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, (Probe.AnswerCount == 1U) && (Probe.Answer == ANSWER_CANCEL));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void TheFocusMovesInAndComesBack(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIButtonOptions Opener = UIButtonOptions_Create(u8"open");
        Opener.Element.HasPlacement = true;
        Opener.Element.Placement = UIPlacement_FromTopLeft(10.0f, 10.0f, 80.0f, 24.0f);
        uint64_t OpenerId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Opener, &OpenerId));
        Widget* OpenerWidget = NULL;
        TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, OpenerId, &OpenerWidget)
            && TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, OpenerWidget)));
        AnswerProbe Probe = { .TextBoxId = WIDGET_ID_INVALID, .ValueBoxId = WIDGET_ID_INVALID };
        uint64_t Id = MakeDialog(run, &Fixture, OkCancel, 3U, WIDGET_ID_INVALID, &Probe);
        uint64_t OkId = WIDGET_ID_INVALID;
        uint64_t LaterId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIDialog_GetButtonId(&Fixture.Screen, Id, ANSWER_OK, &OkId));
        TEST_CHECK_SUCCESS(run, UIDialog_GetButtonId(&Fixture.Screen, Id, ANSWER_LATER, &LaterId));
        TEST_CHECK_ERROR(run, UIDialog_GetButtonId(&Fixture.Screen, Id, 99, &OkId), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_SUCCESS(run, UIDialog_Open(&Fixture.Screen, Id, WIDGET_ID_INVALID));
        TEST_CHECK_ERROR(run, UIDialog_Open(&Fixture.Screen, Id, WIDGET_ID_INVALID), ErrorCode_InvalidOperation);
        TestUI_Ticks(run, &Fixture, 3U);
        // The default button has the focus; Tab moves among the dialog's own; Enter on a focused button answers with it.
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == OkId);
        TestUI_Tap(run, &Fixture, GameKey_Tab); // The first only starts navigation where the focus is.
        TestUI_Tap(run, &Fixture, GameKey_Tab);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == LaterId);
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Probe.AnswerCount == 1U) && (Probe.Answer == ANSWER_LATER));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, !IsOpen(&Fixture, Id) && (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == OpenerId));
        // While open and the topmost popup, a focus that went elsewhere comes back to the default button.
        uint64_t Again = MakeDialog(run, &Fixture, OkCancel, 3U, WIDGET_ID_INVALID, &Probe);
        TEST_CHECK_SUCCESS(run, UIDialog_Open(&Fixture.Screen, Again, WIDGET_ID_INVALID));
        TestUI_Ticks(run, &Fixture, 3U);
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, NULL));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIDialog_GetButtonId(&Fixture.Screen, Again, ANSWER_OK, &OkId));
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == OkId);
        TEST_CHECK_SUCCESS(run, UIDialog_Close(&Fixture.Screen, Again));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, !UIScreen_IsWidgetAlive(&Fixture.Screen, Again) && (Probe.AnswerCount == 1U));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ADestroyedOwnerClosesItUnanswered(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIPanelOptions Panel = UIPanelOptions_Create();
        Panel.Element.HasPlacement = true;
        Panel.Element.Placement = UIPlacement_FromTopLeft(10.0f, 10.0f, 100.0f, 50.0f);
        uint64_t OwnerId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreatePanel(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Panel, &OwnerId));
        TestUI_Tick(run, &Fixture);
        size_t Before = UIScreen_GetWidgetCount(&Fixture.Screen);
        AnswerProbe Probe = { .TextBoxId = WIDGET_ID_INVALID, .ValueBoxId = WIDGET_ID_INVALID };
        uint64_t Id = MakeDialog(run, &Fixture, OkCancel, 3U, OwnerId, &Probe);
        uint64_t Field = AddTextField(run, &Fixture, Id, &Probe);
        TEST_CHECK_SUCCESS(run, UIDialog_Open(&Fixture.Screen, Id, Field));
        TestUI_Ticks(run, &Fixture, 3U);
        TypeText(run, &Fixture, "abc");
        Widget* Owner = NULL;
        TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, OwnerId, &Owner) && TEST_CHECK_SUCCESS(run, Widget_Destroy(Owner)));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, !IsOpen(&Fixture, Id) && !UIScreen_IsWidgetAlive(&Fixture.Screen, Id) && (Probe.AnswerCount == 0U));
        TEST_CHECK(run, UIScreen_GetWidgetCount(&Fixture.Screen) == (Before - 1U));
        // A dialog made and never opened goes when closed, and with the screen otherwise.
        uint64_t Shut = MakeDialog(run, &Fixture, OkCancel, 3U, WIDGET_ID_INVALID, &Probe);
        TEST_CHECK_SUCCESS(run, UIDialog_Close(&Fixture.Screen, Shut));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, !UIScreen_IsWidgetAlive(&Fixture.Screen, Shut));
        MakeDialog(run, &Fixture, OkCancel, 3U, WIDGET_ID_INVALID, &Probe);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ButtonsAnswerByTheMouseAndTheBackdropSwallows(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIButtonOptions Behind = UIButtonOptions_Create(u8"behind");
        Behind.Element.HasPlacement = true;
        Behind.Element.Placement = UIPlacement_FromTopLeft(10.0f, 10.0f, 80.0f, 24.0f);
        uint64_t BehindId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Behind, &BehindId));
        AnswerProbe Probe = { .TextBoxId = WIDGET_ID_INVALID, .ValueBoxId = WIDGET_ID_INVALID };
        uint64_t Id = MakeDialog(run, &Fixture, OkCancel, 3U, WIDGET_ID_INVALID, &Probe);
        TEST_CHECK_SUCCESS(run, UIDialog_Open(&Fixture.Screen, Id, WIDGET_ID_INVALID));
        TestUI_Ticks(run, &Fixture, 3U);
        // A click behind the dialog lands on its backdrop: nothing happens.
        TestUI_Click(run, &Fixture, 40.0f, 20.0f);
        TEST_CHECK(run, (Probe.AnswerCount == 0U) && IsOpen(&Fixture, Id));
        uint64_t CancelId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIDialog_GetButtonId(&Fixture.Screen, Id, ANSWER_CANCEL, &CancelId));
        Rectangle Box = TestUI_GetPixelBox(run, &Fixture, CancelId);
        TEST_CHECK(run, (TestUI_IsSamePixel(Box.width, 70.0f) || (Box.width > 70.0f)) && (Box.height > 0.0f)); // The theme's button width at least.
        TestUI_Click(run, &Fixture, Box.x + (Box.width * 0.5f), Box.y + (Box.height * 0.5f));
        TEST_CHECK(run, (Probe.AnswerCount == 1U) && (Probe.Answer == ANSWER_CANCEL));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, !IsOpen(&Fixture, Id));
        // An answer by code.
        uint64_t Again = MakeDialog(run, &Fixture, OkCancel, 3U, WIDGET_ID_INVALID, &Probe);
        TEST_CHECK_ERROR(run, UIDialog_Answer(&Fixture.Screen, Again, ANSWER_OK), ErrorCode_InvalidOperation);
        TEST_CHECK_SUCCESS(run, UIDialog_Open(&Fixture.Screen, Again, WIDGET_ID_INVALID));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_ERROR(run, UIDialog_Answer(&Fixture.Screen, Again, 42), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_SUCCESS(run, UIDialog_Answer(&Fixture.Screen, Again, ANSWER_LATER));
        TEST_CHECK_SUCCESS(run, UIDialog_Answer(&Fixture.Screen, Again, ANSWER_OK)); // Answered already: nothing more.
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Probe.AnswerCount == 2U) && (Probe.Answer == ANSWER_LATER));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void WithNoCancelEscapeAnswersNothing(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        static const UIDialogButton OnlyOk[] = { { .Id = ANSWER_OK, .Text = u8"OK", .IsDefault = true, .IsCancel = false } };
        AnswerProbe Probe = { .TextBoxId = WIDGET_ID_INVALID, .ValueBoxId = WIDGET_ID_INVALID };
        uint64_t Id = MakeDialog(run, &Fixture, OnlyOk, 1U, WIDGET_ID_INVALID, &Probe);
        uint64_t Field = AddTextField(run, &Fixture, Id, &Probe);
        TEST_CHECK_SUCCESS(run, UIDialog_Open(&Fixture.Screen, Id, Field));
        TestUI_Ticks(run, &Fixture, 3U);
        // Escape in the field leaves it (as the screen does); again, it is swallowed: the dialog stays and the game does not get it.
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, (UIScreen_GetEnteredWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID) && IsOpen(&Fixture, Id) && (Probe.AnswerCount == 0U));
        TestUI_Press(&Fixture, GameKey_Escape);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, !GameInput_IsActionPressed(&Fixture.Input, GameAction_Pause));
        TestUI_Release(&Fixture, GameKey_Escape);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsOpen(&Fixture, Id) && (Probe.AnswerCount == 0U));
        TestUI_Tap(run, &Fixture, GameKey_Enter); // The field only focused: entered first.
        TEST_CHECK(run, (Probe.AnswerCount == 0U) && (UIScreen_GetEnteredWidgetId(&Fixture.Screen) == Field));
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, (Probe.AnswerCount == 1U) && (Probe.Answer == ANSWER_OK));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ATypedValueIsCommittedBeforeTheAnswer(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        AnswerProbe Probe = { .TextBoxId = WIDGET_ID_INVALID, .ValueBoxId = WIDGET_ID_INVALID };
        uint64_t Id = MakeDialog(run, &Fixture, OkCancel, 3U, WIDGET_ID_INVALID, &Probe);
        uint64_t ContentId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIDialog_GetContentId(&Fixture.Screen, Id, &ContentId));
        UIPropertyBoxOptions Box = UIPropertyBoxOptions_Create();
        Box.Element.Hint = UIStackHint_Fixed(24.0f);
        Box.Value.Value = 5.0;
        uint64_t BoxId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreatePropertyBox(&Fixture.Kit, &Fixture.Screen, ContentId, &Box, &BoxId));
        Probe.ValueBoxId = BoxId;
        TEST_CHECK_SUCCESS(run, UIDialog_Open(&Fixture.Screen, Id, BoxId));
        TestUI_Ticks(run, &Fixture, 3U);
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.Screen) == BoxId);
        TypeText(run, &Fixture, "42");
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, (Probe.AnswerCount == 1U) && (Probe.Answer == ANSWER_OK) && (Probe.Value == 42.0));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static uint64_t ButtonOf(TestRun* run, TestUIFixture* fixture, uint64_t dialogId, int32_t answer)
{
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIDialog_GetButtonId(&fixture->Screen, dialogId, answer, &Id));
    return Id;
}

static void ADisabledDefaultOrCancelAnswersNothing(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        AnswerProbe Probe = { .TextBoxId = WIDGET_ID_INVALID, .ValueBoxId = WIDGET_ID_INVALID };
        uint64_t Id = MakeDialog(run, &Fixture, OkCancel, 3U, WIDGET_ID_INVALID, &Probe);
        uint64_t Field = AddTextField(run, &Fixture, Id, &Probe);
        uint64_t Ok = ButtonOf(run, &Fixture, Id, ANSWER_OK);
        uint64_t Later = ButtonOf(run, &Fixture, Id, ANSWER_LATER);
        uint64_t Cancel = ButtonOf(run, &Fixture, Id, ANSWER_CANCEL);
        // OK disabled until a name is typed: opened with no field to focus, the focus goes to the first button that takes it, and every
        // update goes on (each tick checks it).
        TEST_CHECK_SUCCESS(run, UIButton_SetIsEnabled(&Fixture.Screen, Ok, false));
        TEST_CHECK_SUCCESS(run, UIDialog_Open(&Fixture.Screen, Id, WIDGET_ID_INVALID));
        TestUI_Ticks(run, &Fixture, 3U);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == Later);
        // The focused button disabled: the focus goes on to the next that takes it.
        TEST_CHECK_SUCCESS(run, UIButton_SetIsEnabled(&Fixture.Screen, Later, false));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == Cancel);
        // Enter from an entered field with the default disabled: no answer; the field is left as the screen leaves it.
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Field));
        TestUI_Tick(run, &Fixture);
        TypeText(run, &Fixture, "a");
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Probe.AnswerCount == 0U) && IsOpen(&Fixture, Id) && (UIScreen_GetEnteredWidgetId(&Fixture.Screen) != Field));
        // Every button disabled: the dialog leaves the focus where it is, and Escape with the cancel disabled answers nothing either.
        TEST_CHECK_SUCCESS(run, UIButton_SetIsEnabled(&Fixture.Screen, Cancel, false));
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, NULL));
        TestUI_Ticks(run, &Fixture, 2U);
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Probe.AnswerCount == 0U) && IsOpen(&Fixture, Id) && !GameInput_IsActionPressed(&Fixture.Input, GameAction_Pause));
        // OK enabled again: the focus comes to it, and Enter answers with it.
        TEST_CHECK_SUCCESS(run, UIButton_SetIsEnabled(&Fixture.Screen, Ok, true));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == Ok);
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, (Probe.AnswerCount == 1U) && (Probe.Answer == ANSWER_OK));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* The first question's answer asks the next one: a second dialog opened from the first's answer handler. */
typedef struct NextQuestionStruct
{
    uint64_t SecondId;
    size_t SecondAnswers;
} NextQuestion;

static Error OnSecondAnswer(UIScreen* screen, const UIEvent* event, const UserData* userData)
{
    (void)screen;
    NextQuestion* Next = UserData_GetPointer(userData);
    Next->SecondAnswers += ((event->WidgetEventId == (int32_t)UIElementEvent_Activated) && event->HasIndex) ? 1U : 0U;
    return Error_CreateSuccess();
}

static Error OnFirstAnswer(UIScreen* screen, const UIEvent* event, const UserData* userData)
{
    NextQuestion* Next = UserData_GetPointer(userData);
    if ((event->WidgetEventId != (int32_t)UIElementEvent_Activated) || !event->HasIndex || (Next->SecondId != WIDGET_ID_INVALID))
    {
        return Error_CreateSuccess();
    }
    UIDialogOptions Options = UIDialogOptions_Create(u8"Are you sure?");
    Options.Buttons = OkCancel;
    Options.ButtonCount = 3U;
    Error Result = UIKit_CreateDialog(UIKit_FromScreen(screen), screen, &Options, &Next->SecondId);
    uint64_t Subscription = UI_SCREEN_ID_INVALID;
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIScreen_Subscribe(screen, Next->SecondId, UI_EVENT_KIND_MASK(UIEventKind_Widget), OnSecondAnswer, userData, &Subscription);
    }
    return (Result.Code == ErrorCode_Success) ? UIDialog_Open(screen, Next->SecondId, WIDGET_ID_INVALID) : Result;
}

static void ItsCloseLeavesTheNextQuestionOpen(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        // A button behind, focused before the first question opens.
        UIButtonOptions Behind = UIButtonOptions_Create(u8"Close the notes");
        Behind.Element.HasPlacement = true;
        Behind.Element.Placement = UIPlacement_FromTopLeft(20.0f, 20.0f, 160.0f, 28.0f);
        uint64_t BehindId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Behind, &BehindId));
        TestUI_Tick(run, &Fixture);
        Widget* BehindWidget = NULL;
        TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, BehindId, &BehindWidget)
            && TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, BehindWidget)));
        AnswerProbe Probe = { .TextBoxId = WIDGET_ID_INVALID, .ValueBoxId = WIDGET_ID_INVALID };
        NextQuestion Next = { .SecondId = WIDGET_ID_INVALID, .SecondAnswers = 0U };
        uint64_t First = MakeDialog(run, &Fixture, OkCancel, 3U, BehindId, &Probe);
        UserData Data = UserData_FromPointer(&Next);
        uint64_t Subscription = UI_SCREEN_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIScreen_Subscribe(&Fixture.Screen, First, UI_EVENT_KIND_MASK(UIEventKind_Widget), OnFirstAnswer, &Data, &Subscription));
        TEST_CHECK_SUCCESS(run, UIDialog_Open(&Fixture.Screen, First, WIDGET_ID_INVALID));
        TestUI_Ticks(run, &Fixture, 2U);
        // Enter answers the first; its handler asks the second; the first closes alone at the next update.
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, (Probe.AnswerCount == 1U) && !IsOpen(&Fixture, First) && (Next.SecondId != WIDGET_ID_INVALID));
        TEST_CHECK(run, IsOpen(&Fixture, Next.SecondId) && UIScreen_IsWidgetAlive(&Fixture.Screen, Next.SecondId));
        TEST_CHECK(run, UIScreen_FindPopupOf(&Fixture.Screen, UIScreen_GetFocusedWidgetId(&Fixture.Screen)) == Next.SecondId);
        // The second answered and closed: the focus comes back where it was before the first.
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, (Next.SecondAnswers == 1U) && !IsOpen(&Fixture, Next.SecondId));
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == BehindId);
        // Closed by code under a popup that is not its own: that popup stays.
        uint64_t Third = MakeDialog(run, &Fixture, OkCancel, 3U, WIDGET_ID_INVALID, &Probe);
        TEST_CHECK_SUCCESS(run, UIDialog_Open(&Fixture.Screen, Third, WIDGET_ID_INVALID));
        TestUI_Tick(run, &Fixture);
        UIPanelOptions Panel = UIPanelOptions_Create();
        uint64_t PanelId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreatePanel(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Panel, &PanelId));
        UIPopupOptions Above = UIPopupOptions_Create();
        Above.AnchorBox = (Rectangle){ .x = 0.1f, .y = 0.1f, .width = 0.0f, .height = 0.0f };
        Above.OwnerId = BehindId;
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, PanelId, &Above));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIDialog_Close(&Fixture.Screen, Third));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, !IsOpen(&Fixture, Third) && UIScreen_IsPopupOpen(&Fixture.Screen, PanelId));
    }
    TestUI_Deconstruct(run, &Fixture);
}


static void OptionsThatCannotBeMadeAreRefused(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIDialogOptions Options = UIDialogOptions_Create(u8"t");
        uint64_t Id = 7U;
        TEST_CHECK_ERROR(run, UIKit_CreateDialog(&Fixture.Kit, &Fixture.Screen, &Options, &Id), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, Id == WIDGET_ID_INVALID);
        UIDialogButton Two[] = { { .Id = 1, .Text = u8"a", .IsDefault = true }, { .Id = 2, .Text = u8"b", .IsDefault = true } };
        Options.Buttons = Two;
        Options.ButtonCount = 2U;
        TEST_CHECK(run, !UIDialogOptions_IsValid(&Options));
        Two[1].IsDefault = false;
        Two[1].Id = 1;
        TEST_CHECK(run, !UIDialogOptions_IsValid(&Options));
        Two[1].Id = 2;
        Two[1].Text = NULL;
        TEST_CHECK(run, !UIDialogOptions_IsValid(&Options));
        Two[1].Text = u8"b";
        TEST_CHECK(run, UIDialogOptions_IsValid(&Options));
        Options.ButtonCount = UI_DIALOG_MAX_BUTTONS + 1U;
        TEST_CHECK(run, !UIDialogOptions_IsValid(&Options));
        size_t Before = UIScreen_GetWidgetCount(&Fixture.Screen);
        TEST_CHECK_ERROR(run, UIKit_CreateDialog(&Fixture.Kit, &Fixture.Screen, &Options, &Id), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, UIScreen_GetWidgetCount(&Fixture.Screen) == Before);
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Fields.
static const TestCase Cases[] =
{
    { "enter_answers_from_an_entered_field_in_one_press", EnterAnswersFromAnEnteredFieldInOnePress },
    { "escape_cancels_from_an_entered_field_in_one_press", EscapeCancelsFromAnEnteredFieldInOnePress },
    { "enter_on_a_field_only_focused_enters_it", EnterOnAFieldOnlyFocusedEntersIt },
    { "the_focus_moves_in_and_comes_back", TheFocusMovesInAndComesBack },
    { "a_destroyed_owner_closes_it_unanswered", ADestroyedOwnerClosesItUnanswered },
    { "buttons_answer_by_the_mouse_and_the_backdrop_swallows", ButtonsAnswerByTheMouseAndTheBackdropSwallows },
    { "with_no_cancel_escape_answers_nothing", WithNoCancelEscapeAnswersNothing },
    { "a_typed_value_is_committed_before_the_answer", ATypedValueIsCommittedBeforeTheAnswer },
    { "a_disabled_default_or_cancel_answers_nothing", ADisabledDefaultOrCancelAnswersNothing },
    { "its_close_leaves_the_next_question_open", ItsCloseLeavesTheNextQuestionOpen },
    { "options_that_cannot_be_made_are_refused", OptionsThatCannotBeMadeAreRefused },
};


// Public functions.
void UIDialogTests_Run(TestRun* run)
{
    TestRun_RunSuite(run, "UIDialog", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
