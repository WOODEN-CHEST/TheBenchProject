#include "UIDialog.h"
#include "UIButton.h"
#include "UIKit.h"
#include "UIKitInternal.h"
#include "UIPopup.h"
#include "UIScreen.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"


// Static functions: helpers.
static UIDialogCapability* GetCapability(void* widget)
{
    return &((UIDialogLookBase*)widget)->Dialog;
}

static Error ResolveDialog(UIScreen* screen, uint64_t widgetId, Widget** outWidget, UIDialogCapability** outDialog)
{
    UIKit* Kit = (screen != NULL) ? UIKit_FromScreen(screen) : NULL;
    Widget* Target = NULL;
    if ((Kit == NULL) || !UIScreen_TryGetWidgetById(screen, widgetId, &Target))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIDialog: the widget is gone, or its screen has no kit.");
    }
    UIDialogCapability* Dialog = Widget_GetCapability(Target, UIKit_GetCapabilityId(Kit, UIElementCapability_Dialog));
    if (Dialog == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIDialog: the widget is not a dialog.");
    }
    *outWidget = Target;
    *outDialog = Dialog;
    return Error_CreateSuccess();
}

static float CleanLength(float value)
{
    return (Math_IsNaNFloat(value) || Math_IsInfinityFloat(value) || !(value > 0.0f)) ? 0.0f : value;
}

static UserData MakeIdData(uint64_t id)
{
    UserData Data = UserData_CreateEmpty();
    Memory_Copy(&id, Data.Bytes, sizeof(id));
    return Data;
}

static uint64_t ReadIdData(const UserData* data)
{
    uint64_t Id = 0U;
    Memory_Copy(data->Bytes, &Id, sizeof(Id));
    return Id;
}

/* Whether a button can take the focus: there, focusable (a disabled button is not), taking input and shown. */
static bool CanTakeFocus(UIScreen* screen, uint64_t buttonId)
{
    Widget* Button = NULL;
    return UIScreen_TryGetWidgetById(screen, buttonId, &Button) && Widget_IsFocusable(Button) && Widget_IsInputEnabled(Button)
        && Widget_IsRendered(Button);
}

/* The button the focus goes to: the default when it can take it, else the first that can; WIDGET_ID_INVALID for none. */
static uint64_t GetFirstFocus(UIScreen* screen, const UIDialogCapability* self)
{
    if ((self->_defaultIndex >= 0) && CanTakeFocus(screen, self->_buttonIds[self->_defaultIndex]))
    {
        return self->_buttonIds[self->_defaultIndex];
    }
    for (size_t Index = 0U; Index < self->_buttonCount; Index++)
    {
        if (CanTakeFocus(screen, self->_buttonIds[Index]))
        {
            return self->_buttonIds[Index];
        }
    }
    return WIDGET_ID_INVALID;
}

/* Whether a button takes clicks: a disabled default or cancel gives no answer by Enter or Escape either. */
static bool IsButtonEnabled(UIScreen* screen, uint64_t buttonId)
{
    bool IsEnabled = false;
    Error Result = UIButton_IsEnabled(screen, buttonId, &IsEnabled);
    Error_Deconstruct(&Result); // A button gone takes no clicks.
    return IsEnabled;
}

/* Answers with a button: Activated with its answer as the index; the dialog closes once that event was delivered (OnOwnAnswer). */
static Error Answer(Widget* widget, UIDialogCapability* self, size_t buttonIndex)
{
    UIScreen* Screen = Widget_GetScreen(widget);
    uint64_t Id = Widget_GetId(widget);
    if (self->_isAnswered || (buttonIndex >= self->_buttonCount) || !UIScreen_IsPopupOpen(Screen, Id))
    {
        return Error_CreateSuccess();
    }
    self->_isAnswered = true;
    Error Result = UIScreen_PostWidgetIndexedEvent(Screen, Id, UIElementEvent_Activated, NULL, self->_answers[buttonIndex]);
    return (Result.Code == ErrorCode_Success) ? UIElement_PostCue(widget, UIElementCue_Activate) : Result;
}

/* A button of the dialog was activated: the dialog answers with it. */
static Error OnButtonEvent(UIScreen* screen, const UIEvent* event, const UserData* userData)
{
    if (event->WidgetEventId != (int32_t)UIElementEvent_Activated)
    {
        return Error_CreateSuccess();
    }
    Widget* Target = NULL;
    UIDialogCapability* Self = NULL;
    Error Result = ResolveDialog(screen, ReadIdData(userData), &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Error_CreateSuccess(); // Gone with its buttons.
    }
    for (size_t Index = 0U; Index < Self->_buttonCount; Index++)
    {
        if (Self->_buttonIds[Index] == event->WidgetId)
        {
            return Answer(Target, Self, Index);
        }
    }
    return Error_CreateSuccess();
}

/* The dialog's own answer was delivered (every handler has read the fields): it closes at its next update. */
static Error OnOwnAnswer(UIScreen* screen, const UIEvent* event, const UserData* userData)
{
    (void)userData;
    Widget* Target = NULL;
    UIDialogCapability* Self = NULL;
    if ((event->WidgetEventId != (int32_t)UIElementEvent_Activated) || !event->HasIndex)
    {
        return Error_CreateSuccess();
    }
    Error Result = ResolveDialog(screen, event->WidgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Error_CreateSuccess();
    }
    Self->_isClosePending = Self->_isAnswered;
    return Error_CreateSuccess();
}

/* Whether the screen's entered element lies inside the dialog. */
static bool IsEnteredInside(const Widget* widget)
{
    UIScreen* Screen = Widget_GetScreen(widget);
    uint64_t Entered = UIScreen_GetEnteredWidgetId(Screen);
    return (Entered != WIDGET_ID_INVALID) && (UIScreen_FindPopupOf(Screen, Entered) == Widget_GetId(widget));
}

static Error PlaceAt(UIScreen* screen, uint64_t id, float x, float y, float width, float height)
{
    Widget* Part = NULL;
    if (!UIScreen_TryGetWidgetById(screen, id, &Part))
    {
        return Error_CreateSuccess();
    }
    UIPlacement Placement = UIPlacement_FromTopLeft(x, y, Math_MaxFloat(0.0f, width), Math_MaxFloat(0.0f, height));
    return Widget_SetPlacement(Part, &Placement);
}

static Error MeasurePart(UIScreen* screen, uint64_t id, const UIMeasureContext* context, float width, Vector2* outSize)
{
    *outSize = (Vector2){ .x = 0.0f, .y = 0.0f };
    Widget* Part = NULL;
    if (!UIScreen_TryGetWidgetById(screen, id, &Part))
    {
        return Error_CreateSuccess();
    }
    return Widget_Measure(Part, context, width, outSize);
}

/* The buttons' row: each as wide as it measures and the least width at least, as tall as the tallest. */
static Error MeasureButtons(UIScreen* screen, const UIDialogCapability* self, const UIMeasureContext* context, float* outWidths, Vector2* outRow)
{
    *outRow = (Vector2){ .x = 0.0f, .y = 0.0f };
    for (size_t Index = 0U; Index < self->_buttonCount; Index++)
    {
        Vector2 Size = { .x = 0.0f, .y = 0.0f };
        Error Result = MeasurePart(screen, self->_buttonIds[Index], context, UI_MEASURE_UNBOUNDED_WIDTH, &Size);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        outWidths[Index] = Math_MaxFloat(Size.x, self->_metrics.ButtonWidth);
        outRow->x += outWidths[Index] + ((Index > 0U) ? self->_metrics.ButtonSpacing : 0.0f);
        outRow->y = Math_MaxFloat(outRow->y, Size.y);
    }
    return Error_CreateSuccess();
}

/* The heights of the parts in the column at an inner width: the title, the message, the content, the buttons; each 0 when it does not show. */
static Error MeasureColumn(UIScreen* screen, const UIDialogCapability* self, const UIMeasureContext* context, float innerWidth, float* outHeights)
{
    Vector2 Size = { .x = 0.0f, .y = 0.0f };
    Error Result = self->_hasTitle ? MeasurePart(screen, self->_titleId, context, innerWidth, &Size) : Error_CreateSuccess();
    outHeights[0] = self->_hasTitle ? Size.y : 0.0f;
    if ((Result.Code == ErrorCode_Success) && self->_hasMessage)
    {
        Result = MeasurePart(screen, self->_messageId, context, innerWidth, &Size);
    }
    outHeights[1] = self->_hasMessage ? Size.y : 0.0f;
    Size = (Vector2){ .x = 0.0f, .y = 0.0f };
    if (Result.Code == ErrorCode_Success)
    {
        Result = MeasurePart(screen, self->_contentId, context, innerWidth, &Size);
    }
    outHeights[2] = Size.y;
    return Result;
}


// Static functions: the behaviour.
static void* ResolveDialogCapability(void* widget)
{
    return GetCapability(widget);
}

static Error Behaviour_Construct(void* widget, void* args)
{
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIDialog: the arguments are required.");
    }
    const UIDialogOptions* Options = ((const UIDialogArgs*)args)->Options;
    if (!UIDialogOptions_IsValid(Options))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "UIDialog: one to UI_DIALOG_MAX_BUTTONS buttons with texts and different ids, at most one default and one cancel, are required.");
    }
    Error Result = UIElement_ApplyOptions(widget, &Options->Element);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    UIDialogCapability* Self = GetCapability(widget);
    Self->_titleId = WIDGET_ID_INVALID;
    Self->_messageId = WIDGET_ID_INVALID;
    Self->_contentId = WIDGET_ID_INVALID;
    Self->_buttonCount = 0U;
    Self->_defaultIndex = -1;
    Self->_cancelIndex = -1;
    for (size_t Index = 0U; Index < UI_DIALOG_MAX_BUTTONS; Index++)
    {
        Self->_buttonIds[Index] = WIDGET_ID_INVALID;
        Self->_answers[Index] = 0;
    }
    Self->_hasTitle = (Options->Title != NULL) && (Options->Title[0] != 0U);
    Self->_hasMessage = (Options->Message != NULL) && (Options->Message[0] != 0U);
    Self->_ownerId = Options->OwnerId;
    Self->_isOpened = false;
    Self->_isAnswered = false;
    Self->_isClosePending = false;
    Self->_metrics = (UIDialogMetrics){ .Padding = { .x = 14.0f, .y = 12.0f }, .Spacing = 10.0f, .ButtonSpacing = 8.0f, .ButtonWidth = 80.0f,
        .MinSize = { .x = 240.0f, .y = 0.0f }, .MaxWidth = 480.0f };
    return Error_CreateSuccess();
}

/* An answered dialog closes; an open one that is the topmost popup keeps the focus inside it. */
static Error Behaviour_Update(void* widget, ProgramTime time)
{
    (void)time;
    UIDialogCapability* Self = GetCapability(widget);
    UIScreen* Screen = Widget_GetScreen(widget);
    uint64_t Id = Widget_GetId(widget);
    if (!UIScreen_IsPopupOpen(Screen, Id))
    {
        return Error_CreateSuccess();
    }
    if (Self->_isClosePending)
    {
        return UIScreen_ClosePopupAndItsOwn(Screen, Id); // Not what its answer opened above it.
    }
    if (Self->_isAnswered)
    {
        return Error_CreateSuccess(); // Its answer is on its way: the focus is left alone meanwhile.
    }
    size_t Count = UIScreen_GetPopupCount(Screen);
    uint64_t Focused = UIScreen_GetFocusedWidgetId(Screen);
    bool IsTopmost = (Count > 0U) && (UIScreen_GetPopupAt(Screen, Count - 1U) == Id);
    if (!IsTopmost || ((Focused != WIDGET_ID_INVALID) && (UIScreen_FindPopupOf(Screen, Focused) == Id)))
    {
        return Error_CreateSuccess();
    }
    Widget* First = NULL;
    return UIScreen_TryGetWidgetById(Screen, GetFirstFocus(Screen, Self), &First) ? UIScreen_SetFocusedWidget(Screen, First) : Error_CreateSuccess();
}

/* The title, the message and the content in a column from the top; the buttons in a row at the bottom right. */
static Error Behaviour_Layout(void* widget, const UILayoutContext* context)
{
    UIDialogCapability* Self = GetCapability(widget);
    UIScreen* Screen = Widget_GetScreen(widget);
    Rectangle Pixels = UIScreen_GetWidgetPixelBox(Screen, widget);
    float Scale = Widget_GetEffectiveScale(widget);
    Vector2 Box = { .x = Pixels.width / Scale, .y = Pixels.height / Scale };
    const UIDialogMetrics* Metrics = &Self->_metrics;
    float Inner = Math_MaxFloat(0.0f, Box.x - (2.0f * Metrics->Padding.x));
    float Heights[3] = { 0.0f, 0.0f, 0.0f };
    Error Result = MeasureColumn(Screen, Self, context->Sizing, Inner, Heights);
    const uint64_t Parts[3] = { Self->_titleId, Self->_messageId, Self->_contentId };
    float Y = Metrics->Padding.y;
    for (size_t Index = 0U; (Index < 3U) && (Result.Code == ErrorCode_Success); Index++)
    {
        Result = PlaceAt(Screen, Parts[Index], Metrics->Padding.x, Y, Inner, Heights[Index]);
        Y += (Heights[Index] > 0.0f) ? (Heights[Index] + Metrics->Spacing) : 0.0f;
    }
    float Widths[UI_DIALOG_MAX_BUTTONS];
    Vector2 Row = { .x = 0.0f, .y = 0.0f };
    if (Result.Code == ErrorCode_Success)
    {
        Result = MeasureButtons(Screen, Self, context->Sizing, Widths, &Row);
    }
    float X = Box.x - Metrics->Padding.x - Row.x;
    float RowY = Math_MaxFloat(Y, Box.y - Metrics->Padding.y - Row.y);
    for (size_t Index = 0U; (Index < Self->_buttonCount) && (Result.Code == ErrorCode_Success); Index++)
    {
        Result = PlaceAt(Screen, Self->_buttonIds[Index], X, RowY, Widths[Index], Row.y);
        X += Widths[Index] + Metrics->ButtonSpacing;
    }
    return Result;
}

/* Enter takes the default and Escape the cancel, in one press from inside an entered field: the field is left first (kept, or cancelled). */
static Error Behaviour_OnKeyboardInput(void* widget, const WidgetKeyboardInputArgs* args, bool* outIsHandled)
{
    UIDialogCapability* Self = GetCapability(widget);
    UIScreen* Screen = Widget_GetScreen(widget);
    if ((args->Type != UIKeyboardInputType_KeyPress) || args->IsHoverDelivery || !UIScreen_IsPopupOpen(Screen, Widget_GetId(widget)))
    {
        return Error_CreateSuccess();
    }
    bool IsAccept = UIElement_IsActionKey(Screen, GameAction_UIAccept, args->Key);
    bool IsBack = UIElement_IsActionKey(Screen, GameAction_UIBack, args->Key);
    if (!IsAccept && !IsBack)
    {
        return Error_CreateSuccess();
    }
    int32_t Button = IsAccept ? Self->_defaultIndex : Self->_cancelIndex;
    if ((Button >= 0) && !IsButtonEnabled(Screen, Self->_buttonIds[Button]))
    {
        Button = -1; // A disabled one answers nothing, as if there were none.
    }
    bool IsEntered = IsEnteredInside(widget);
    if (Button < 0)
    {
        // No such button: Enter and Escape leave an entered field as the screen does; with none entered, Escape is swallowed.
        *outIsHandled = IsBack && !IsEntered;
        return Error_CreateSuccess();
    }
    *outIsHandled = true;
    uint64_t Id = Widget_GetId(widget);
    if (IsEntered)
    {
        Error Result = UIScreen_LeaveWidget(Screen, IsAccept ? UILeaveReason_Keep : UILeaveReason_Cancel);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    Widget* Found = NULL;
    return UIScreen_TryGetWidgetById(Screen, Id, &Found) ? Answer(Found, GetCapability(Found), (size_t)Button) : Error_CreateSuccess();
}


// Fields.
static const UIKindCapability DialogCapabilities[] =
{
    { .Capability = UIElementCapability_Dialog, .Resolve = ResolveDialogCapability }
};

static const UIKindBehaviour DialogBehaviour =
{
    .Kind = UIElementKind_Dialog,
    .Capabilities = DialogCapabilities,
    .CapabilityCount = sizeof(DialogCapabilities) / sizeof(DialogCapabilities[0]),
    .BaseSize = sizeof(UIDialogLookBase),
    .IsFocusable = false,
    .NeedsEntering = false,
    .EntersItself = false,
    .Construct = Behaviour_Construct,
    .Update = Behaviour_Update,
    .Layout = Behaviour_Layout,
    .OnKeyboardInput = Behaviour_OnKeyboardInput
};


// Internal functions (declared in UIKitInternal.h).
const UIKindBehaviour* UIDialog_GetKindBehaviour(void)
{
    return &DialogBehaviour;
}


// Public functions: calling code.
Error UIDialog_Open(UIScreen* screen, uint64_t widgetId, uint64_t focusId)
{
    Widget* Target = NULL;
    UIDialogCapability* Self = NULL;
    Error Result = ResolveDialog(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (Self->_isOpened)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIDialog_Open: a dialog opens once; make a new one.");
    }
    UIPopupOptions Options = UIPopupOptions_Create();
    Options.AnchorBox = (Rectangle){ .x = 0.0f, .y = 0.0f, .width = 1.0f, .height = 1.0f };
    Options.Side = UIPopupSide_Center;
    Options.Flips = false;
    Options.IsModal = true;
    Options.ClosesOnEscape = false; // The dialog answers Escape itself.
    Options.OwnerId = Self->_ownerId;
    Options.FocusId = (focusId != WIDGET_ID_INVALID) ? focusId : GetFirstFocus(screen, Self);
    Self->_isOpened = true;
    return UIScreen_OpenPopup(screen, widgetId, &Options);
}

Error UIDialog_Close(UIScreen* screen, uint64_t widgetId)
{
    Widget* Target = NULL;
    UIDialogCapability* Self = NULL;
    Error Result = ResolveDialog(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return UIScreen_IsPopupOpen(screen, widgetId) ? UIScreen_ClosePopupAndItsOwn(screen, widgetId) : Widget_Destroy(Target);
}

Error UIDialog_Answer(UIScreen* screen, uint64_t widgetId, int32_t answer)
{
    Widget* Target = NULL;
    UIDialogCapability* Self = NULL;
    Error Result = ResolveDialog(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!UIScreen_IsPopupOpen(screen, widgetId))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIDialog_Answer: the dialog is not open.");
    }
    for (size_t Index = 0U; Index < Self->_buttonCount; Index++)
    {
        if (Self->_answers[Index] == answer)
        {
            return Answer(Target, Self, Index);
        }
    }
    return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIDialog_Answer: no button gives that answer.");
}

Error UIDialog_GetContentId(UIScreen* screen, uint64_t widgetId, uint64_t* outContentId)
{
    Widget* Target = NULL;
    UIDialogCapability* Self = NULL;
    Error Result = ResolveDialog(screen, widgetId, &Target, &Self);
    if (Result.Code == ErrorCode_Success)
    {
        *outContentId = Self->_contentId;
    }
    return Result;
}

Error UIDialog_GetButtonId(UIScreen* screen, uint64_t widgetId, int32_t answer, uint64_t* outButtonId)
{
    Widget* Target = NULL;
    UIDialogCapability* Self = NULL;
    Error Result = ResolveDialog(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    for (size_t Index = 0U; Index < Self->_buttonCount; Index++)
    {
        if (Self->_answers[Index] == answer)
        {
            *outButtonId = Self->_buttonIds[Index];
            return Error_CreateSuccess();
        }
    }
    return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIDialog_GetButtonId: no button gives that answer.");
}


// Public functions: behaviour helpers.
void UIDialogCapability_SetMetrics(UIDialogCapability* self, Widget* widget, const UIDialogMetrics* metrics)
{
    UIDialogMetrics Clean =
    {
        .Padding = { .x = CleanLength(metrics->Padding.x), .y = CleanLength(metrics->Padding.y) },
        .Spacing = CleanLength(metrics->Spacing),
        .ButtonSpacing = CleanLength(metrics->ButtonSpacing),
        .ButtonWidth = CleanLength(metrics->ButtonWidth),
        .MinSize = { .x = CleanLength(metrics->MinSize.x), .y = CleanLength(metrics->MinSize.y) },
        .MaxWidth = CleanLength(metrics->MaxWidth)
    };
    if (Memory_IsEqual(&Clean, &self->_metrics, sizeof(Clean)))
    {
        return;
    }
    self->_metrics = Clean;
    Widget_InvalidateMeasure(widget);
}

Error UIDialog_Measure(Widget* widget, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    UIDialogCapability* Self = GetCapability(widget);
    UIScreen* Screen = Widget_GetScreen(widget);
    const UIDialogMetrics* Metrics = &Self->_metrics;
    // As wide as its widest part, within its least width and the widest it grows (and the room it has).
    float Widths[UI_DIALOG_MAX_BUTTONS];
    Vector2 Row = { .x = 0.0f, .y = 0.0f };
    Error Result = MeasureButtons(Screen, Self, context, Widths, &Row);
    float Natural = Row.x;
    const uint64_t Parts[3] = { Self->_titleId, Self->_messageId, Self->_contentId };
    const bool Shows[3] = { Self->_hasTitle, Self->_hasMessage, true };
    for (size_t Index = 0U; (Index < 3U) && (Result.Code == ErrorCode_Success); Index++)
    {
        Vector2 Size = { .x = 0.0f, .y = 0.0f };
        if (Shows[Index])
        {
            Result = MeasurePart(Screen, Parts[Index], context, UI_MEASURE_UNBOUNDED_WIDTH, &Size);
        }
        Natural = Math_MaxFloat(Natural, Size.x);
    }
    float Widest = Math_MaxFloat(Metrics->MinSize.x, Math_MinFloat(Metrics->MaxWidth, availableWidth));
    float Width = Math_MinFloat(Math_MaxFloat(Natural + (2.0f * Metrics->Padding.x), Metrics->MinSize.x), Widest);
    float Heights[3] = { 0.0f, 0.0f, 0.0f };
    if (Result.Code == ErrorCode_Success)
    {
        Result = MeasureColumn(Screen, Self, context, Math_MaxFloat(0.0f, Width - (2.0f * Metrics->Padding.x)), Heights);
    }
    float Height = (2.0f * Metrics->Padding.y) + Row.y;
    for (size_t Index = 0U; Index < 3U; Index++)
    {
        Height += (Heights[Index] > 0.0f) ? (Heights[Index] + Metrics->Spacing) : 0.0f;
    }
    outSize->x = Width;
    outSize->y = Math_MaxFloat(Height, Metrics->MinSize.y);
    return Result;
}

Error UIDialog_SetParts(UIScreen* screen, uint64_t widgetId, uint64_t titleId, uint64_t messageId, uint64_t contentId,
    const uint64_t* buttonIds, const UIDialogButton* buttons, size_t buttonCount)
{
    Widget* Target = NULL;
    UIDialogCapability* Self = NULL;
    Error Result = ResolveDialog(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Self->_titleId = titleId;
    Self->_messageId = messageId;
    Self->_contentId = contentId;
    Self->_buttonCount = Math_MinSizeT(buttonCount, UI_DIALOG_MAX_BUTTONS);
    for (size_t Index = 0U; Index < Self->_buttonCount; Index++)
    {
        Self->_buttonIds[Index] = buttonIds[Index];
        Self->_answers[Index] = buttons[Index].Id;
        Self->_defaultIndex = buttons[Index].IsDefault ? (int32_t)Index : Self->_defaultIndex;
        Self->_cancelIndex = buttons[Index].IsCancel ? (int32_t)Index : Self->_cancelIndex;
    }
    UserData Data = MakeIdData(widgetId);
    uint64_t Subscription = UI_SCREEN_ID_INVALID;
    Result = UIScreen_Subscribe(screen, widgetId, UI_EVENT_KIND_MASK(UIEventKind_Widget), OnOwnAnswer, NULL, &Subscription);
    for (size_t Index = 0U; (Index < buttonCount) && (Result.Code == ErrorCode_Success); Index++)
    {
        Result = UIScreen_Subscribe(screen, buttonIds[Index], UI_EVENT_KIND_MASK(UIEventKind_Widget), OnButtonEvent, &Data, &Subscription);
    }
    return Result;
}

bool UIDialogOptions_IsValid(const UIDialogOptions* options)
{
    if ((options->Buttons == NULL) || (options->ButtonCount == 0U) || (options->ButtonCount > UI_DIALOG_MAX_BUTTONS))
    {
        return false;
    }
    size_t Defaults = 0U;
    size_t Cancels = 0U;
    for (size_t Index = 0U; Index < options->ButtonCount; Index++)
    {
        const UIDialogButton* Button = &options->Buttons[Index];
        if ((Button->Text == NULL) || !StringUTF8_IsEncodingValid(Button->Text))
        {
            return false;
        }
        for (size_t Other = 0U; Other < Index; Other++)
        {
            if (options->Buttons[Other].Id == Button->Id)
            {
                return false;
            }
        }
        Defaults += Button->IsDefault ? 1U : 0U;
        Cancels += Button->IsCancel ? 1U : 0U;
    }
    return (Defaults <= 1U) && (Cancels <= 1U);
}
