#include "UIDropdown.h"
#include "Logger.h"
#include "UIKit.h"
#include "UIKitInternal.h"
#include "UIPopup.h"
#include "UIRowRecycler.h"
#include "UIScreen.h"
#include "UIScrollable.h"
#include "UIScrollbar.h"
#include "UIWidgetFactory.h"
#include "wr/WRMath.h"
#include "wr/WRString.h"


// Macros.
/** What the head shows while the choice is mixed, unless its options say otherwise. */
#define DEFAULT_MIXED_TEXT ((const unsigned char*)u8"-")
/** The narrowest an open list's text column is, in UI pixels, before its anchor makes it wider. */
#define LIST_MIN_TEXT_WIDTH 48.0f
/** How far, in UI pixels, the rows may pass the list's room and still fit: float noise shows no bar. */
#define FIT_TOLERANCE 0.01f


// Types.
/* What a list is made with. */
typedef struct DropdownListArgsStruct
{
    UIKit* Kit;
    uint64_t HeadId;
    const unsigned char* Variant;
} DropdownListArgs;

/* What a row is made with. */
typedef struct DropdownRowArgsStruct
{
    UIKit* Kit;
    uint64_t ListId;
    const unsigned char* Variant;
} DropdownRowArgs;

/* A dropdown's open list: a popup's root, an edit element entered as it opens. Its rows are recycled widgets inside a box that cuts them,
 * its bar a scroll bar bound to it. */
typedef struct DropdownListStruct
{
    Widget Base;
    UIScrollableCapability Scrollable;
    UIElementLook Look;
    UIKit* Kit;
    uint64_t HeadId;
    uint64_t RowsId;
    uint64_t BarId;
    UIRowRecycler Rows;
    bool HasHighlight;
    size_t Highlight;
    bool IsRevealPending;
    uint64_t ShownRevision;
    bool HasStrips;
    float HeldStrip;
    double StripCountdownSeconds;
    GenericBuffer Scratch;
    /* The press a release over a row may choose by: the head's that opened the list (only once it moved past a click's distance: a
     * click on a list placed over its head chooses nothing), or one that began on a row; and its button. */
    bool IsHeadPressHeld;
    bool IsRowPressHeld;
    GameKey PressButton;
    /* Where the pointer was at the last update: the highlight follows it when it moves, not rows scrolled under it. */
    bool WasPointerPresent;
    Vector2 LastPointer;
    /* How many options the rows were bound for: a provider whose count changes is asked for their texts again. */
    size_t ShownCount;
    /* A row a mouse release chose, acted on at the list's next update: the row stays under the pointer until the screen has delivered
     * that release, so what lies behind the closing list hears nothing of it. */
    bool HasPendingChoice;
    size_t PendingChoice;
} DropdownList;

/* One row of an open list: the option it shows, highlighted or selected. */
typedef struct DropdownRowStruct
{
    Widget Base;
    UIElementLook Look;
    UIElementText Text;
    UITextContent Content;
    uint64_t ListId;
    size_t Index;
    bool IsBound;
} DropdownRow;


// Static functions: helpers.
static UIDropdownCapability* GetCapability(void* widget)
{
    return &((UIDropdownLookBase*)widget)->Dropdown;
}

static float RoundToPixel(float value)
{
    return Math_FloorFloat(value + 0.5f);
}

static Error ResolveDropdown(UIScreen* screen, uint64_t widgetId, Widget** outWidget, UIDropdownCapability** outDropdown)
{
    UIKit* Kit = (screen != NULL) ? UIKit_FromScreen(screen) : NULL;
    Widget* Target = NULL;
    if ((Kit == NULL) || !UIScreen_TryGetWidgetById(screen, widgetId, &Target))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIDropdown: the widget is gone, or its screen has no kit.");
    }
    UIDropdownCapability* Dropdown = Widget_GetCapability(Target, UIKit_GetCapabilityId(Kit, UIElementCapability_Dropdown));
    if (Dropdown == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIDropdown: the widget is not a dropdown.");
    }
    *outWidget = Target;
    *outDropdown = Dropdown;
    return Error_CreateSuccess();
}

/* The head a list belongs to, and its capability; false when it is gone. */
static bool TryGetHead(const DropdownList* list, Widget** outHead, UIDropdownCapability** outDropdown)
{
    UIScreen* Screen = Widget_GetScreen(&list->Base);
    Widget* Head = NULL;
    if (!UIScreen_TryGetWidgetById(Screen, list->HeadId, &Head))
    {
        return false;
    }
    UIDropdownCapability* Dropdown = Widget_GetCapability(Head, UIKit_GetCapabilityId(list->Kit, UIElementCapability_Dropdown));
    if (Dropdown == NULL)
    {
        return false;
    }
    *outHead = Head;
    *outDropdown = Dropdown;
    return true;
}

static const UIThemeStyle* GetListStyle(const DropdownList* list)
{
    return UIElementLook_GetStyle(&list->Look, Widget_GetScreen(&list->Base));
}

/* A row's height, in the list's UI pixels: the theme's, never below one. */
static float GetRowHeight(const UIThemeStyle* style)
{
    return Math_MaxFloat(1.0f, UIThemeStyle_GetNumber(style, UIElementKind_Dropdown, UIDropdownPart_RowHeight));
}

static float GetStripHeight(const UIThemeStyle* style)
{
    return UIThemeStyle_GetNumber(style, UIElementKind_Dropdown, UIDropdownPart_ScrollArrowHeight);
}

static size_t GetOptionCount(const DropdownList* list)
{
    Widget* Head = NULL;
    UIDropdownCapability* Dropdown = NULL;
    return TryGetHead(list, &Head, &Dropdown) ? UIChoiceCapability_GetCount(&Dropdown->Choice, Head) : 0U;
}

/* The bar's thickness, in the list's UI pixels: what its look measures. */
static Error MeasureBar(const DropdownList* list, const UIMeasureContext* sizing, float* outThickness)
{
    *outThickness = 0.0f;
    Widget* Bar = NULL;
    if (!UIScreen_TryGetWidgetById(Widget_GetScreen(&list->Base), list->BarId, &Bar))
    {
        return Error_CreateSuccess();
    }
    Vector2 Size = { .x = 0.0f, .y = 0.0f };
    Error Result = Widget_Measure(Bar, sizing, UI_MEASURE_UNBOUNDED_WIDTH, &Size);
    *outThickness = Size.x;
    return Result;
}

/* Scrolls the list as little as it takes to show the highlighted row, with the view as it was last laid out. */
static void RevealHighlight(DropdownList* list)
{
    if (!list->HasHighlight)
    {
        return;
    }
    float RowHeight = GetRowHeight(GetListStyle(list));
    Rectangle Box = { .x = 0.0f, .y = (float)((double)list->Highlight * (double)RowHeight), .width = 1.0f, .height = RowHeight };
    UIScrollableCapability_EnsureVisible(&list->Scrollable, Box);
}

static void SetHighlight(DropdownList* list, size_t index)
{
    list->HasHighlight = true;
    list->Highlight = index;
    RevealHighlight(list);
}

/* Chooses an option of the list's head as the user did, and closes the list (the focus goes back to the head). */
static Error ChooseAndClose(DropdownList* list, size_t index)
{
    UIScreen* Screen = Widget_GetScreen(&list->Base);
    uint64_t ListId = Widget_GetId(&list->Base);
    Widget* Head = NULL;
    UIDropdownCapability* Dropdown = NULL;
    if (TryGetHead(list, &Head, &Dropdown))
    {
        bool HasChanged = false;
        Error Result = UIChoiceCapability_Choose(&Dropdown->Choice, Head, index, &HasChanged);
        if (Result.Code == ErrorCode_Success)
        {
            Result = UIElement_PostCue(Head, UIElementCue_Activate);
        }
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return UIScreen_IsPopupOpen(Screen, ListId) ? UIScreen_ClosePopup(Screen, ListId) : Error_CreateSuccess();
}


// Static functions: the head's behaviour, for the kit's composed widget types.
static void* ResolveChoice(void* widget)
{
    return &GetCapability(widget)->Choice;
}

static void* ResolveDropdownCapability(void* widget)
{
    return GetCapability(widget);
}

/* Reads the selected option's text (or the mixed text) into what the head shows, when the choice changed. */
static Error RefreshShownText(UIDropdownCapability* self, Widget* widget)
{
    if (self->_shownRevision == self->Choice._revision)
    {
        return Error_CreateSuccess();
    }
    GenericBuffer_Clear(&self->_scratch);
    Error Result = Error_CreateSuccess();
    if (self->Choice._isMixed)
    {
        const unsigned char* Mixed = UITextContent_Get(&self->_mixedText);
        // The cast drops a const the framework's signature lacks: the bytes are only read.
        if (!GenericBuffer_AppendRangeBytes(&self->_scratch, (unsigned char*)Mixed, StringUTF8_GetByteLength(Mixed))
            || !GenericBuffer_NullTerminate(&self->_scratch))
        {
            Result = Error_Construct2(ErrorCode_BufferTooLarge, "UIDropdown: the text buffer could not grow.");
        }
    }
    else if (self->Choice._hasSelection)
    {
        Result = UIChoiceCapability_AppendText(&self->Choice, widget, self->Choice._selected, &self->_scratch);
    }
    else if (!GenericBuffer_NullTerminate(&self->_scratch))
    {
        Result = Error_Construct2(ErrorCode_BufferTooLarge, "UIDropdown: the text buffer could not grow.");
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    bool HasChanged = false;
    Result = UITextContent_Set(&self->_shownText, self->_scratch._data, &HasChanged);
    if ((Result.Code == ErrorCode_Success) && HasChanged)
    {
        Widget_InvalidateMeasure(widget);
    }
    self->_shownRevision = self->Choice._revision;
    return Result;
}

/* The variant of the head's look, which its list and rows are drawn with; NULL for the kind's own. */
static const unsigned char* GetHeadVariant(const UIKit* kit, Widget* head)
{
    const UIElementLook* Look = Widget_GetCapability(head, UIKit_GetCapabilityId(kit, UIElementCapability_Look));
    return ((Look != NULL) && (Look->_variant[0] != 0U)) ? Look->_variant : NULL;
}

/* Makes a widget of one of the kit's own types and initializes it, detached. */
static Error CreateDetached(UIScreen* screen, UIKit* kit, uint64_t typeId, void* args, uint64_t* outId)
{
    Widget* Made = NULL;
    Error Result = UIWidgetFactory_CreateWidget(kit->_factory, screen, typeId, args, &Made);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    *outId = Widget_GetId(Made);
    Result = Widget_InitializeWidget(Made);
    if (Result.Code != ErrorCode_Success)
    {
        Error Cleanup = Widget_Destroy(Made);
        Error_Deconstruct(&Cleanup);
    }
    return Result;
}

/* Makes a list's parts: the box that cuts its rows, and its bar on top. */
static Error BuildListParts(UIScreen* screen, UIKit* kit, uint64_t listId)
{
    Widget* List = NULL;
    Widget* Rows = NULL;
    uint64_t RowsId = WIDGET_ID_INVALID;
    Error Result = CreateDetached(screen, kit, kit->_clipType, NULL, &RowsId);
    if ((Result.Code == ErrorCode_Success) && UIScreen_TryGetWidgetById(screen, listId, &List) && UIScreen_TryGetWidgetById(screen, RowsId, &Rows))
    {
        Result = Widget_AddSubWidget(List, Rows);
    }
    UIScrollbarOptions Bar = UIScrollbarOptions_Create();
    Bar.TargetId = listId;
    Bar.Element.HasPlacement = true;
    Bar.Element.Placement = UIPlacement_FromTopLeft(0.0f, 0.0f, 0.0f, 0.0f);
    uint64_t BarId = WIDGET_ID_INVALID;
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIKit_CreateScrollbar(kit, screen, listId, &Bar, &BarId);
    }
    Widget* Made = NULL;
    if ((Result.Code == ErrorCode_Success) && UIScreen_TryGetWidgetById(screen, BarId, &Made))
    {
        Result = Widget_SetZLayer(Made, 1.0f);
    }
    if ((Result.Code == ErrorCode_Success) && UIScreen_TryGetWidgetById(screen, listId, &List))
    {
        DropdownList* Box = (DropdownList*)(void*)List;
        Box->RowsId = RowsId;
        Box->BarId = BarId;
    }
    return Result;
}

/* Opens the head's list, entered: nothing when it is open, disabled or has no options, and nothing (said once, when it happens) with as
 * many popups open as a screen holds. @p pressButton is the button of the head's press that opened it, GameKey_Unknown for a key or code. */
static Error OpenList(UIScreen* screen, uint64_t headId, GameKey pressButton)
{
    Widget* Head = NULL;
    UIDropdownCapability* Dropdown = NULL;
    Error Result = ResolveDropdown(screen, headId, &Head, &Dropdown);
    if ((Result.Code != ErrorCode_Success) || !Dropdown->_isEnabled || UIScreen_IsPopupOpen(screen, Dropdown->_listId)
        || (UIChoiceCapability_GetCount(&Dropdown->Choice, Head) == 0U))
    {
        return Result;
    }
    if (UIScreen_GetPopupCount(screen) >= UI_SCREEN_MAX_POPUPS)
    {
        Logger* Log = UIScreen_GetLogger(screen);
        const unsigned char* Message = (const unsigned char*)u8"UIDropdown: a list did not open: as many popups as a screen holds are open.";
        Error LogResult = (Log != NULL) ? Logger_LogWarning(Log, Message) : Error_CreateSuccess();
        Error_Deconstruct(&LogResult);
        return Error_CreateSuccess();
    }
    UIDropdownDirection Direction = Dropdown->_direction; // Read now: making the list runs hooks.
    UIKit* Kit = UIKit_FromScreen(screen);
    DropdownListArgs Arguments = { .Kit = Kit, .HeadId = headId, .Variant = GetHeadVariant(Kit, Head) };
    uint64_t ListId = WIDGET_ID_INVALID;
    Result = CreateDetached(screen, Kit, Kit->_dropdownListType, &Arguments, &ListId);
    if (Result.Code == ErrorCode_Success)
    {
        Result = BuildListParts(screen, Kit, ListId);
    }
    Widget* List = NULL;
    if ((Result.Code == ErrorCode_Success) && UIScreen_TryGetWidgetById(screen, ListId, &List))
    {
        const UIThemeStyle* Style = GetListStyle((DropdownList*)(void*)List);
        Vector2 Padding = UIThemeStyle_GetVector(Style, UIElementKind_Dropdown, UIDropdownPart_ListPadding);
        UIPopupOptions Options = UIPopupOptions_Create();
        Options.AnchorWidgetId = headId;
        Options.Side = (Direction == UIDropdownDirection_Up) ? UIPopupSide_Above : UIPopupSide_Below;
        Options.Flips = Direction == UIDropdownDirection_Auto;
        Options.AllowsShrinking = true;
        Options.MinimumLength = GetRowHeight(Style) + (2.0f * Padding.y) + (2.0f * GetStripHeight(Style));
        Options.MatchesAnchorWidth = true;
        Options.OwnerId = headId;
        Options.IsOwnerInside = true;
        Options.FocusId = ListId;
        Result = UIScreen_OpenPopup(screen, ListId, &Options);
    }
    if (Result.Code != ErrorCode_Success)
    {
        if (UIScreen_TryGetWidgetById(screen, ListId, &List) && !UIScreen_IsPopupOpen(screen, ListId))
        {
            Error Cleanup = Widget_Destroy(List);
            Error_Deconstruct(&Cleanup);
        }
        return Result;
    }
    if ((pressButton != GameKey_Unknown) && UIScreen_TryGetWidgetById(screen, ListId, &List))
    {
        DropdownList* Opened = (DropdownList*)(void*)List;
        Opened->IsHeadPressHeld = true;
        Opened->PressButton = pressButton;
    }
    // Opening ran hooks: the head is found again.
    Result = ResolveDropdown(screen, headId, &Head, &Dropdown);
    if (Result.Code == ErrorCode_Success)
    {
        Dropdown->_listId = ListId;
    }
    Error_Deconstruct(&Result);
    return Error_CreateSuccess();
}

static Error CloseList(UIScreen* screen, UIDropdownCapability* self)
{
    uint64_t ListId = self->_listId;
    self->_listId = WIDGET_ID_INVALID;
    return UIScreen_IsPopupOpen(screen, ListId) ? UIScreen_ClosePopup(screen, ListId) : Error_CreateSuccess();
}

static Error Behaviour_Construct(void* widget, void* args)
{
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIDropdown: the arguments are required.");
    }
    const UIDropdownOptions* Options = ((const UIDropdownArgs*)args)->Options;
    bool IsValid = (Options->VisibleCount >= 1U) && (Options->VisibleCount <= UI_DROPDOWN_MAX_VISIBLE_COUNT)
        && ((int32_t)Options->Direction >= (int32_t)UIDropdownDirection_Auto) && ((int32_t)Options->Direction <= (int32_t)UIDropdownDirection_Down);
    if (!IsValid)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIDropdown: a visible count within [1, UI_DROPDOWN_MAX_VISIBLE_COUNT] and a known direction are required.");
    }
    UIDropdownCapability* Self = GetCapability(widget);
    Error Result = UIElement_ApplyOptions(widget, &Options->Element);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = UIChoiceCapability_Construct(&Self->Choice, Options->Options, Options->OptionCount, Options->HasSelection, Options->Selected);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = UITextContent_Construct(&Self->_mixedText, (Options->MixedText != NULL) ? Options->MixedText : DEFAULT_MIXED_TEXT);
    if (Result.Code != ErrorCode_Success)
    {
        UIChoiceCapability_Deconstruct(&Self->Choice);
        return Result;
    }
    Result = UITextContent_Construct(&Self->_shownText, NULL);
    if (Result.Code != ErrorCode_Success)
    {
        UITextContent_Deconstruct(&Self->_mixedText);
        UIChoiceCapability_Deconstruct(&Self->Choice);
        return Result;
    }
    GenericBuffer_AllocateVariable(&Self->_scratch, 64U, sizeof(unsigned char));
    Self->_press = UIPressable_Create();
    Self->_press._clickMode = UIClickMode_OnPress;
    Self->_visibleCount = Options->VisibleCount;
    Self->_direction = Options->Direction;
    Self->_isEnabled = Options->IsEnabled;
    Self->_shownRevision = 0U;
    Self->_listId = WIDGET_ID_INVALID;
    if (!Options->IsEnabled)
    {
        Error Focus = Widget_SetIsFocusable(widget, false); // The widget is not alive yet: nothing can fail or run.
        Error_Deconstruct(&Focus);
    }
    return Error_CreateSuccess();
}

static Error Behaviour_Deconstruct(void* widget)
{
    UIDropdownCapability* Self = GetCapability(widget);
    UIChoiceCapability_Deconstruct(&Self->Choice);
    UITextContent_Deconstruct(&Self->_mixedText);
    UITextContent_Deconstruct(&Self->_shownText);
    Memory_Free(Self->_scratch._data);
    return Error_CreateSuccess();
}

static Error Behaviour_Update(void* widget, ProgramTime time)
{
    (void)time;
    UIDropdownCapability* Self = GetCapability(widget);
    UIScreen* Screen = Widget_GetScreen(widget);
    UIPressable_Update(&Self->_press, Screen);
    if ((Self->_listId != WIDGET_ID_INVALID) && !UIScreen_IsPopupOpen(Screen, Self->_listId))
    {
        Self->_listId = WIDGET_ID_INVALID; // Closed by the popup layer: a press outside, Escape, a choice.
    }
    return RefreshShownText(Self, widget);
}

static Error Behaviour_OnHoverStart(void* widget, const WidgetHoverArgs* args)
{
    (void)args;
    return UIElement_PostCue(widget, UIElementCue_Hover);
}

/* A left press opens the list at once, or closes it; a press, a drag to a row and a release there chooses that row. */
static Error Behaviour_OnMouseInput(void* widget, const WidgetMouseInputArgs* args)
{
    UIDropdownCapability* Self = GetCapability(widget);
    Error Result = Self->_isEnabled ? UIElement_PostMouseEvents(widget, args) : Error_CreateSuccess();
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIElement_PostMouseCues(widget, args, Self->_isEnabled);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    bool IsActivated = false;
    UIPressable_OnMouseInput(&Self->_press, widget, args, Self->_isEnabled, &IsActivated);
    if (!IsActivated)
    {
        return Error_CreateSuccess();
    }
    UIScreen* Screen = Widget_GetScreen(widget);
    return UIScreen_IsPopupOpen(Screen, Self->_listId) ? CloseList(Screen, Self) : OpenList(Screen, Widget_GetId(widget), args->Button);
}

static Error Behaviour_OnPressLost(void* widget, GameKey button)
{
    UIPressable_OnPressLost(&GetCapability(widget)->_press, button);
    return Error_CreateSuccess();
}

/* Ctrl with the wheel steps through the options while the list is closed, a whole change each notch; a plain wheel goes on up the tree. */
static Error Behaviour_OnScrollInput(void* widget, const WidgetScrollInputArgs* args, bool* outIsHandled)
{
    UIDropdownCapability* Self = GetCapability(widget);
    if ((args->Modifiers & GAME_KEY_MODIFIER_CONTROL) == 0U)
    {
        return Error_CreateSuccess();
    }
    *outIsHandled = true;
    size_t Count = UIChoiceCapability_GetCount(&Self->Choice, widget);
    if (!Self->_isEnabled || (Count == 0U) || UIScreen_IsPopupOpen(Widget_GetScreen(widget), Self->_listId) || (args->ScrollDelta.y == 0.0f))
    {
        return Error_CreateSuccess();
    }
    // A notch down is the next option, a notch up the one before.
    size_t Target = 0U;
    if (!Self->Choice._hasSelection)
    {
        Target = (args->ScrollDelta.y < 0.0f) ? 0U : (Count - 1U);
    }
    else if (args->ScrollDelta.y < 0.0f)
    {
        Target = (Self->Choice._selected + 1U < Count) ? (Self->Choice._selected + 1U) : (Count - 1U);
    }
    else
    {
        Target = (Self->Choice._selected > 0U) ? (Self->Choice._selected - 1U) : 0U;
    }
    bool HasChanged = false;
    Error Result = UIChoiceCapability_Choose(&Self->Choice, widget, Target, &HasChanged);
    return ((Result.Code == ErrorCode_Success) && HasChanged) ? UIElement_PostCue(widget, UIElementCue_ValueStep) : Result;
}

/* Enter or Space opens the list, entered; the arrows go past it to navigation. */
static Error Behaviour_OnKeyboardInput(void* widget, const WidgetKeyboardInputArgs* args, bool* outIsHandled)
{
    UIDropdownCapability* Self = GetCapability(widget);
    bool IsActivated = false;
    UIPressable_OnKeyboardInput(&Self->_press, widget, args, Self->_isEnabled, outIsHandled, &IsActivated);
    if (IsActivated)
    {
        return OpenList(Widget_GetScreen(widget), Widget_GetId(widget), GameKey_Unknown);
    }
    return (Self->_isEnabled && !*outIsHandled) ? UIElement_OnContextKey(widget, args, outIsHandled) : Error_CreateSuccess();
}


// Static functions: the list.
static Error List_Construct(void* widget, void* args)
{
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIDropdown: a list's arguments are required.");
    }
    const DropdownListArgs* Arguments = args;
    DropdownList* List = widget;
    Error Result = UIElementLook_Construct(&List->Look, UIElementKind_Dropdown, Arguments->Variant);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    UIScrollableCapability_Construct(&List->Scrollable, UIScrollMode_Disabled, UIScrollMode_Auto, UI_SCROLLABLE_DEFAULT_WHEEL_STEP);
    UIRowRecycler_Construct(&List->Rows);
    List->Kit = Arguments->Kit;
    List->HeadId = Arguments->HeadId;
    List->RowsId = WIDGET_ID_INVALID;
    List->BarId = WIDGET_ID_INVALID;
    List->HasHighlight = false;
    List->Highlight = 0U;
    List->IsRevealPending = true;
    List->ShownRevision = 0U;
    List->HasStrips = false;
    List->HeldStrip = 0.0f;
    List->StripCountdownSeconds = 0.0;
    GenericBuffer_AllocateVariable(&List->Scratch, 64U, sizeof(unsigned char));
    List->IsHeadPressHeld = false;
    List->IsRowPressHeld = false;
    List->PressButton = GameKey_Unknown;
    List->ShownCount = 0U;
    List->HasPendingChoice = false;
    List->PendingChoice = 0U;
    // The highlight starts at the selected option, and follows the pointer only once it moves.
    UIScreen* Screen = Widget_GetScreen(&List->Base);
    List->WasPointerPresent = UIScreen_IsPointerPresent(Screen);
    List->LastPointer = UIScreen_GetPointerPosition(Screen);
    Widget* Head = NULL;
    if (UIScreen_TryGetWidgetById(Screen, Arguments->HeadId, &Head))
    {
        const UIDropdownCapability* Dropdown = Widget_GetCapability(Head, UIKit_GetCapabilityId(Arguments->Kit, UIElementCapability_Dropdown));
        if ((Dropdown != NULL) && Dropdown->Choice._hasSelection)
        {
            List->HasHighlight = true;
            List->Highlight = Dropdown->Choice._selected;
        }
    }
    return Error_CreateSuccess();
}

static Error List_OnDeconstruct(void* self)
{
    DropdownList* List = self;
    UIRowRecycler_Deconstruct(&List->Rows);
    Memory_Free(List->Scratch._data);
    return Error_CreateSuccess();
}

static Error Row_Bind(void* owner, uint64_t widgetId, size_t dataIndex);
static Error Row_Unbind(void* owner, uint64_t widgetId);
static Error Row_Create(void* owner, uint64_t* outWidgetId);

static const UIRowRecyclerHooks RowHooks = { .CreateRow = Row_Create, .BindRow = Row_Bind, .UnbindRow = Row_Unbind };

static Error List_Update(void* self, ProgramTime time)
{
    DropdownList* List = self;
    UIScreen* Screen = Widget_GetScreen(&List->Base);
    UIElementLook_Update(&List->Look, Screen, UIElementState_Normal, time.PassedTime);
    Widget* Head = NULL;
    UIDropdownCapability* Dropdown = NULL;
    if (!TryGetHead(List, &Head, &Dropdown))
    {
        return Error_CreateSuccess(); // Gone: the popup layer closes the list.
    }
    if (List->HasPendingChoice)
    {
        List->HasPendingChoice = false;
        return ChooseAndClose(List, List->PendingChoice);
    }
    size_t Count = UIChoiceCapability_GetCount(&Dropdown->Choice, Head);
    if ((Dropdown->Choice._revision != List->ShownRevision) || (Count != List->ShownCount))
    {
        UIRowRecycler_Invalidate(&List->Rows); // The options changed under the rows.
        List->ShownRevision = Dropdown->Choice._revision;
        List->ShownCount = Count;
    }
    if (List->HasHighlight && (List->Highlight >= Count))
    {
        List->HasHighlight = Count > 0U;
        List->Highlight = (Count > 0U) ? (Count - 1U) : 0U;
    }
    // A held strip scrolls on; the press a row may be chosen by ends with its button.
    const GameInput* Input = UIScreen_GetInput(Screen);
    if ((List->IsHeadPressHeld || List->IsRowPressHeld) && (Input != NULL)
        && ((GameInput_GetRawKeyState(Input, List->PressButton) & GAME_INPUT_KEY_STATE_DOWN) == 0U))
    {
        List->IsHeadPressHeld = false;
        List->IsRowPressHeld = false;
    }
    if ((List->HeldStrip != 0.0f) && (Input != NULL) && ((GameInput_GetRawKeyState(Input, GameKey_MouseLeft) & GAME_INPUT_KEY_STATE_DOWN) == 0U))
    {
        List->HeldStrip = 0.0f;
    }
    if (List->HeldStrip != 0.0f)
    {
        List->StripCountdownSeconds -= time.PassedTime;
        float RowHeight = GetRowHeight(GetListStyle(List));
        while (List->StripCountdownSeconds <= 0.0)
        {
            UIScrollableCapability_ScrollBy(&List->Scrollable, (Vector2){ .x = 0.0f, .y = List->HeldStrip * RowHeight });
            List->StripCountdownSeconds += UI_DROPDOWN_STRIP_REPEAT_SECONDS;
        }
    }
    Error Result = UIRowRecycler_Sync(&List->Rows, Screen, Count, GetRowHeight(GetListStyle(List)), List->Scrollable._offset.y,
        List->Scrollable._viewSize.y, &RowHooks, List);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    // The highlight follows the pointer when it moves: rows scrolled under a pointer at rest leave it where the keys put it.
    bool IsPointerPresent = UIScreen_IsPointerPresent(Screen);
    Vector2 Pointer = UIScreen_GetPointerPosition(Screen);
    bool HasPointerMoved = IsPointerPresent && (!List->WasPointerPresent || (Pointer.x != List->LastPointer.x) || (Pointer.y != List->LastPointer.y));
    List->WasPointerPresent = IsPointerPresent;
    List->LastPointer = Pointer;
    for (size_t Index = 0U; HasPointerMoved && (Index < UIRowRecycler_GetCount(&List->Rows)); Index++)
    {
        const UIRecycledRow* Row = UIRowRecycler_GetAt(&List->Rows, Index);
        Widget* RowWidget = NULL;
        if (Row->IsBound && UIScreen_TryGetWidgetById(Screen, Row->WidgetId, &RowWidget) && Widget_IsHovered(RowWidget))
        {
            List->HasHighlight = true;
            List->Highlight = Row->DataIndex;
        }
    }
    return Error_CreateSuccess();
}

/* Its rows box (between the strips when it scrolls, left of the bar), its bar, and its rows at the offset. */
static Error List_Layout(void* self, const UILayoutContext* context)
{
    DropdownList* List = self;
    UIScreen* Screen = Widget_GetScreen(&List->Base);
    const UIThemeStyle* Style = GetListStyle(List);
    float Scale = Widget_GetEffectiveScale(&List->Base);
    Rectangle Pixels = UIScreen_GetWidgetPixelBox(Screen, &List->Base);
    Vector2 Box = { .x = Pixels.width / Scale, .y = Pixels.height / Scale };
    Vector2 Padding = UIThemeStyle_GetVector(Style, UIElementKind_Dropdown, UIDropdownPart_ListPadding);
    float RowHeight = GetRowHeight(Style);
    float BarThickness = 0.0f;
    Error Result = MeasureBar(List, context->Sizing, &BarThickness);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    double Content = (double)GetOptionCount(List) * (double)RowHeight;
    float InnerWidth = Math_MaxFloat(0.0f, Box.x - (2.0f * Padding.x));
    float InnerHeight = Math_MaxFloat(0.0f, Box.y - (2.0f * Padding.y));
    List->HasStrips = Content > (double)(InnerHeight + FIT_TOLERANCE);
    float Strip = List->HasStrips ? GetStripHeight(Style) : 0.0f;
    float Bar = List->HasStrips ? BarThickness : 0.0f;
    Rectangle Rows = { .x = Padding.x, .y = Padding.y + Strip, .width = Math_MaxFloat(0.0f, InnerWidth - Bar),
        .height = Math_MaxFloat(0.0f, InnerHeight - (2.0f * Strip)) };
    UIScrollableCapability_SetSizes(&List->Scrollable, (Vector2){ .x = Rows.width, .y = (float)Content }, (Vector2){ .x = Rows.width, .y = Rows.height });
    List->Scrollable._wheelStep = RowHeight * UI_DROPDOWN_WHEEL_ROWS;
    if (List->IsRevealPending && (Rows.height > 0.0f))
    {
        RevealHighlight(List);
        List->IsRevealPending = false;
    }
    Widget* RowsWidget = NULL;
    if (UIScreen_TryGetWidgetById(Screen, List->RowsId, &RowsWidget))
    {
        UIPlacement Placement = UIPlacement_FromTopLeft(Rows.x, Rows.y, Rows.width, Rows.height);
        Result = Widget_SetPlacement(RowsWidget, &Placement);
    }
    Widget* BarWidget = NULL;
    if ((Result.Code == ErrorCode_Success) && UIScreen_TryGetWidgetById(Screen, List->BarId, &BarWidget))
    {
        UIPlacement Placement = UIPlacement_FromTopLeft(Rows.x + Rows.width, Rows.y, Bar, List->HasStrips ? Rows.height : 0.0f);
        Result = Widget_SetPlacement(BarWidget, &Placement);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIRowRecycler_Place(&List->Rows, Screen, RowHeight, List->Scrollable._offset.y);
    }
    List->Scrollable._placedOffset = List->Scrollable._offset;
    List->Scrollable._viewWidgetId = List->RowsId;
    return Result;
}

static Error List_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)availableWidth;
    DropdownList* List = self;
    const UIThemeStyle* Style = GetListStyle(List);
    Widget* Head = NULL;
    UIDropdownCapability* Dropdown = NULL;
    size_t Count = 0U;
    size_t Visible = UI_DROPDOWN_DEFAULT_VISIBLE_COUNT;
    if (TryGetHead(List, &Head, &Dropdown))
    {
        Count = UIChoiceCapability_GetCount(&Dropdown->Choice, Head);
        Visible = Dropdown->_visibleCount;
    }
    float BarThickness = 0.0f;
    Error Result = MeasureBar(List, context, &BarThickness);
    Vector2 Padding = UIThemeStyle_GetVector(Style, UIElementKind_Dropdown, UIDropdownPart_ListPadding);
    bool IsScrolling = Count > Visible;
    size_t Rows = IsScrolling ? Visible : Count;
    outSize->x = (2.0f * Padding.x) + LIST_MIN_TEXT_WIDTH + (IsScrolling ? BarThickness : 0.0f);
    outSize->y = ((float)Rows * GetRowHeight(Style)) + (2.0f * Padding.y) + (IsScrolling ? (2.0f * GetStripHeight(Style)) : 0.0f);
    return Result;
}

static Error List_Render(void* self, UIRenderContext* context)
{
    DropdownList* List = self;
    Rectangle Pixels = UIRenderContext_GetPixelBox(context);
    if (UIElement_IsBoxEmpty(Pixels))
    {
        return Error_CreateSuccess();
    }
    const UIThemeStyle* Style = GetListStyle(List);
    float Scale = Widget_GetEffectiveScale(&List->Base);
    Error Result = UIElementLook_DrawBox(&List->Look, Style, UIDropdownPart_List, context, Pixels, Scale);
    if ((Result.Code != ErrorCode_Success) || !List->HasStrips)
    {
        return Result;
    }
    // The strips' arrows, each only while more lies that way; the strips keep their room, so the rows never jump.
    Vector2 Padding = UIThemeStyle_GetVector(Style, UIElementKind_Dropdown, UIDropdownPart_ListPadding);
    float Strip = RoundToPixel(GetStripHeight(Style) * Scale);
    float Arrow = RoundToPixel(UIThemeStyle_GetNumber(Style, UIElementKind_Dropdown, UIDropdownPart_ArrowSize) * Scale);
    float Top = Pixels.y + RoundToPixel(Padding.y * Scale);
    float Bottom = (Pixels.y + Pixels.height) - RoundToPixel(Padding.y * Scale) - Strip;
    float Left = Pixels.x + RoundToPixel((Pixels.width - Arrow) * 0.5f);
    const UIStateColors* ArrowColors = UIThemeStyle_GetColors(Style, UIDropdownPart_Arrow);
    Color Tint = (ArrowColors != NULL) ? UIStateColors_Resolve(ArrowColors, UIElementState_Normal, WHITE) : WHITE;
    Vector2 Range = UIScrollableCapability_GetRange(&List->Scrollable);
    if (List->Scrollable._offset.y > 0.0f)
    {
        UIElement_DrawArrow(context, (Rectangle){ .x = Left, .y = Top, .width = Arrow, .height = Strip }, UIArrowDirection_Up, Tint);
    }
    if (List->Scrollable._offset.y < Range.y)
    {
        UIElement_DrawArrow(context, (Rectangle){ .x = Left, .y = Bottom, .width = Arrow, .height = Strip }, UIArrowDirection_Down, Tint);
    }
    return Error_CreateSuccess();
}

/* A press on a strip scrolls a row, and holding it goes on. */
static Error List_OnMouseInput(void* self, const WidgetMouseInputArgs* args)
{
    DropdownList* List = self;
    if ((args->Button != GameKey_MouseLeft) || !List->HasStrips)
    {
        return Error_CreateSuccess();
    }
    if (args->Type == UIMouseInputType_ButtonRelease)
    {
        List->HeldStrip = 0.0f;
        return Error_CreateSuccess();
    }
    if (args->Type != UIMouseInputType_ButtonPress)
    {
        return Error_CreateSuccess();
    }
    const UIThemeStyle* Style = GetListStyle(List);
    float Height = UIScreen_GetWidgetPixelBox(Widget_GetScreen(&List->Base), &List->Base).height / Widget_GetEffectiveScale(&List->Base);
    float Y = args->WidgetPosition.y * Height;
    float PaddingY = UIThemeStyle_GetVector(Style, UIElementKind_Dropdown, UIDropdownPart_ListPadding).y;
    float Strip = GetStripHeight(Style);
    float Direction = 0.0f;
    if ((Y >= PaddingY) && (Y < (PaddingY + Strip)))
    {
        Direction = -1.0f;
    }
    else if ((Y > (Height - PaddingY - Strip)) && (Y <= (Height - PaddingY)))
    {
        Direction = 1.0f;
    }
    if (Direction != 0.0f)
    {
        UIScrollableCapability_ScrollBy(&List->Scrollable, (Vector2){ .x = 0.0f, .y = Direction * GetRowHeight(Style) });
        List->HeldStrip = Direction;
        List->StripCountdownSeconds = UI_DROPDOWN_STRIP_DELAY_SECONDS;
    }
    return Error_CreateSuccess();
}

static Error List_OnPressLost(void* self, GameKey button)
{
    if (button == GameKey_MouseLeft)
    {
        ((DropdownList*)self)->HeldStrip = 0.0f;
    }
    return Error_CreateSuccess();
}

/* The wheel scrolls the list a few rows a notch; it is the list's whatever happens (nothing lies under a popup's root). */
static Error List_OnScrollInput(void* self, const WidgetScrollInputArgs* args, bool* outIsHandled)
{
    DropdownList* List = self;
    *outIsHandled = true;
    UIScrollableCapability_ScrollBy(&List->Scrollable, (Vector2){ .x = 0.0f, .y = -args->ScrollDelta.y * List->Scrollable._wheelStep });
    return Error_CreateSuccess();
}

/* Moves the highlight by rows, kept to the options. */
static void MoveHighlight(DropdownList* list, size_t count, int64_t rows)
{
    if (count == 0U)
    {
        return;
    }
    int64_t From = list->HasHighlight ? (int64_t)list->Highlight : ((rows > 0) ? -1 : (int64_t)count);
    int64_t Target = From + rows;
    Target = (Target < 0) ? 0 : ((Target >= (int64_t)count) ? ((int64_t)count - 1) : Target);
    SetHighlight(list, (size_t)Target);
}

static bool IsTypingKey(GameKey key)
{
    return (((int32_t)key >= (int32_t)GameKey_A) && ((int32_t)key <= (int32_t)GameKey_Z))
        || (((int32_t)key >= (int32_t)GameKey_0) && ((int32_t)key <= (int32_t)GameKey_9));
}

/* Entered as it opens: Up and Down move the highlight, Home and End to the ends, Page Up and Page Down a list's height, a letter to the
 * next option starting with it, Enter chooses, Tab closes; Escape is the popup layer's. */
static Error List_OnKeyboardInput(void* self, const WidgetKeyboardInputArgs* args, bool* outIsHandled)
{
    DropdownList* List = self;
    UIScreen* Screen = Widget_GetScreen(&List->Base);
    Widget* Head = NULL;
    UIDropdownCapability* Dropdown = NULL;
    if (!TryGetHead(List, &Head, &Dropdown))
    {
        return Error_CreateSuccess();
    }
    size_t Count = UIChoiceCapability_GetCount(&Dropdown->Choice, Head);
    if (args->Type == UIKeyboardInputType_Text)
    {
        *outIsHandled = true;
        size_t Found = 0U;
        bool IsFound = false;
        Error Result = UIChoiceCapability_FindByLetter(&Dropdown->Choice, Head, args->Codepoint, List->HasHighlight, List->Highlight,
            &List->Scratch, &Found, &IsFound);
        if ((Result.Code == ErrorCode_Success) && IsFound)
        {
            SetHighlight(List, Found);
        }
        return Result;
    }
    bool IsDown = (args->Type == UIKeyboardInputType_KeyPress) || (args->Type == UIKeyboardInputType_KeyRepeat);
    if (!IsDown)
    {
        return Error_CreateSuccess();
    }
    float RowHeight = GetRowHeight(GetListStyle(List));
    int64_t Page = (int64_t)Math_MaxFloat(1.0f, Math_FloorFloat(List->Scrollable._viewSize.y / RowHeight));
    *outIsHandled = true;
    if (UIElement_IsActionKey(Screen, GameAction_UIUp, args->Key))
    {
        MoveHighlight(List, Count, -1);
    }
    else if (UIElement_IsActionKey(Screen, GameAction_UIDown, args->Key))
    {
        MoveHighlight(List, Count, 1);
    }
    else if (args->Key == GameKey_PageUp)
    {
        MoveHighlight(List, Count, -Page);
    }
    else if (args->Key == GameKey_PageDown)
    {
        MoveHighlight(List, Count, Page);
    }
    else if ((args->Key == GameKey_Home) && (Count > 0U))
    {
        SetHighlight(List, 0U);
    }
    else if ((args->Key == GameKey_End) && (Count > 0U))
    {
        SetHighlight(List, Count - 1U);
    }
    else if ((args->Type == UIKeyboardInputType_KeyPress) && UIElement_IsActionKey(Screen, GameAction_UIAccept, args->Key))
    {
        return List->HasHighlight ? ChooseAndClose(List, List->Highlight) : UIScreen_ClosePopup(Screen, Widget_GetId(&List->Base));
    }
    else if ((args->Type == UIKeyboardInputType_KeyPress)
        && (UIElement_IsActionKey(Screen, GameAction_UINext, args->Key) || UIElement_IsActionKey(Screen, GameAction_UIPrevious, args->Key)))
    {
        return UIScreen_ClosePopup(Screen, Widget_GetId(&List->Base));
    }
    else
    {
        // The press of a key that types a letter is the list's too, so it reaches nothing behind while the letter jumps.
        *outIsHandled = (args->Type == UIKeyboardInputType_KeyPress) && IsTypingKey(args->Key);
    }
    return Error_CreateSuccess();
}

static void* List_ResolveScrollable(void* widget)
{
    return &((DropdownList*)widget)->Scrollable;
}


// Static functions: the rows.
static Error Row_Construct(void* widget, void* args)
{
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIDropdown: a row's arguments are required.");
    }
    const DropdownRowArgs* Arguments = args;
    DropdownRow* Row = widget;
    Error Result = UIElementLook_Construct(&Row->Look, UIElementKind_Dropdown, Arguments->Variant);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UITextContent_Construct(&Row->Content, NULL);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = UIElementText_Construct(&Row->Text, UIKit_GetTextFactory(Arguments->Kit), UIKit_GetAssets(Arguments->Kit),
        UIKit_GetLogger(Arguments->Kit));
    if (Result.Code != ErrorCode_Success)
    {
        UITextContent_Deconstruct(&Row->Content);
        return Result;
    }
    Row->ListId = Arguments->ListId;
    Row->Index = 0U;
    Row->IsBound = false;
    return Error_CreateSuccess();
}

static Error Row_OnDeconstruct(void* self)
{
    DropdownRow* Row = self;
    UITextContent_Deconstruct(&Row->Content);
    return UIElementText_Deconstruct(&Row->Text);
}

static DropdownList* GetRowList(const DropdownRow* row)
{
    Widget* List = NULL;
    return UIScreen_TryGetWidgetById(Widget_GetScreen(&row->Base), row->ListId, &List) ? (DropdownList*)(void*)List : NULL;
}

static UIElementState GetRowState(const DropdownRow* row)
{
    const DropdownList* List = GetRowList(row);
    Widget* Head = NULL;
    UIDropdownCapability* Dropdown = NULL;
    bool IsSelected = (List != NULL) && TryGetHead(List, &Head, &Dropdown) && Dropdown->Choice._hasSelection && !Dropdown->Choice._isMixed
        && (Dropdown->Choice._selected == row->Index);
    bool IsHighlighted = (List != NULL) && List->HasHighlight && (List->Highlight == row->Index);
    return UIElementState_Choose(true, false, false, false, IsSelected, IsHighlighted, false);
}

static Error Row_Update(void* self, ProgramTime time)
{
    DropdownRow* Row = self;
    UIElementLook_Update(&Row->Look, Widget_GetScreen(&Row->Base), GetRowState(Row), time.PassedTime);
    return UIElementText_Refresh(&Row->Text, &Row->Base, &Row->Content);
}

static Error Row_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)context;
    (void)availableWidth;
    DropdownRow* Row = self;
    Error Result = UIElementText_Refresh(&Row->Text, &Row->Base, &Row->Content);
    const UIScreen* Screen = Widget_GetScreen(&Row->Base);
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Row->Look, Screen);
    Vector2 Text = UIElementText_GetPixelSize(&Row->Text, Screen);
    float Padding = UIThemeStyle_GetVector(Style, UIElementKind_Dropdown, UIDropdownPart_Padding).x;
    outSize->x = (Text.x / Widget_GetEffectiveScale(&Row->Base)) + (2.0f * Padding);
    outSize->y = GetRowHeight(Style);
    return Result;
}

static Error Row_Render(void* self, UIRenderContext* context)
{
    DropdownRow* Row = self;
    Rectangle Pixels = UIRenderContext_GetPixelBox(context);
    if (!Row->IsBound || UIElement_IsBoxEmpty(Pixels))
    {
        return Error_CreateSuccess();
    }
    const UIScreen* Screen = Widget_GetScreen(&Row->Base);
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Row->Look, Screen);
    float Scale = Widget_GetEffectiveScale(&Row->Base);
    Error Result = UIElementLook_DrawBox(&Row->Look, Style, UIDropdownPart_Row, context, Pixels, Scale);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Vector2 Text = UIElementText_GetPixelSize(&Row->Text, Screen);
    float Padding = RoundToPixel(UIThemeStyle_GetVector(Style, UIElementKind_Dropdown, UIDropdownPart_Padding).x * Scale);
    Color TextColor = UIElementLook_BlendColors(&Row->Look, UIThemeStyle_GetColors(Style, UIDropdownPart_RowText), WHITE);
    RenderContext_PushClip(UIRenderContext_GetRenderContext(context), Pixels);
    Result = UIElementText_Draw(&Row->Text, context, Pixels.x + Padding, Pixels.y + RoundToPixel((Pixels.height - Text.y) * 0.5f), TextColor);
    RenderContext_PopClip(UIRenderContext_GetRenderContext(context));
    return Result;
}

static Error Row_OnHoverStart(void* self, const WidgetHoverArgs* args)
{
    (void)args;
    return UIElement_PostCue(&((DropdownRow*)self)->Base, UIElementCue_Hover);
}

/* Whether a press went further than a click's distance from where it began: a drag. */
static bool IsDrag(const Widget* widget, const WidgetMouseInputArgs* args)
{
    Vector2 Surface = UIScreen_GetViewport(Widget_GetScreen(widget)).SizePixels;
    float Across = (args->ScreenPosition.x - args->ClickStartScreenPosition.x) * Surface.x;
    float Down = (args->ScreenPosition.y - args->ClickStartScreenPosition.y) * Surface.y;
    float Limit = UI_SCREEN_MULTI_CLICK_DISTANCE * Widget_GetEffectiveScale(widget);
    return ((Across * Across) + (Down * Down)) > (Limit * Limit);
}

/* A release over the row chooses it: its own press let go on it, a press that began on another row, or the head's press that opened the
 * list once it was dragged (press, drag, release); a click on a list placed over its head, or a release of the bar's or a strip's press,
 * chooses nothing. */
static Error Row_OnMouseInput(void* self, const WidgetMouseInputArgs* args)
{
    DropdownRow* Row = self;
    bool IsChoosingButton = (args->Button == GameKey_MouseLeft) || (args->Button == GameKey_MouseRight);
    DropdownList* List = GetRowList(Row);
    if (!IsChoosingButton || (List == NULL))
    {
        return Error_CreateSuccess();
    }
    if (args->Type == UIMouseInputType_ButtonPress)
    {
        List->IsRowPressHeld = true;
        List->PressButton = args->Button;
        return Error_CreateSuccess();
    }
    bool IsOver = (args->WidgetPosition.x >= 0.0f) && (args->WidgetPosition.x <= 1.0f) && (args->WidgetPosition.y >= 0.0f)
        && (args->WidgetPosition.y <= 1.0f);
    bool IsOwnRelease = (args->Type == UIMouseInputType_ButtonRelease) && IsOver;
    bool IsChosenOver = (args->Type == UIMouseInputType_ReleaseOver)
        && (List->IsRowPressHeld || (List->IsHeadPressHeld && IsDrag(&Row->Base, args)));
    if (Row->IsBound && (IsOwnRelease || IsChosenOver))
    {
        List->HasPendingChoice = true;
        List->PendingChoice = Row->Index;
    }
    return Error_CreateSuccess();
}

static Error Row_Create(void* owner, uint64_t* outWidgetId)
{
    DropdownList* List = owner;
    UIScreen* Screen = Widget_GetScreen(&List->Base);
    DropdownRowArgs Arguments = { .Kit = List->Kit, .ListId = Widget_GetId(&List->Base), .Variant = (List->Look._variant[0] != 0U) ? List->Look._variant : NULL };
    uint64_t RowId = WIDGET_ID_INVALID;
    Error Result = CreateDetached(Screen, List->Kit, List->Kit->_dropdownRowType, &Arguments, &RowId);
    Widget* Rows = NULL;
    Widget* Made = NULL;
    if ((Result.Code == ErrorCode_Success) && UIScreen_TryGetWidgetById(Screen, List->RowsId, &Rows) && UIScreen_TryGetWidgetById(Screen, RowId, &Made))
    {
        Result = Widget_AddSubWidget(Rows, Made);
    }
    if ((Result.Code != ErrorCode_Success) && UIScreen_TryGetWidgetById(Screen, RowId, &Made))
    {
        Error Cleanup = Widget_Destroy(Made);
        Error_Deconstruct(&Cleanup);
    }
    *outWidgetId = RowId;
    return Result;
}

static Error Row_Bind(void* owner, uint64_t widgetId, size_t dataIndex)
{
    DropdownList* List = owner;
    UIScreen* Screen = Widget_GetScreen(&List->Base);
    Widget* Found = NULL;
    Widget* Head = NULL;
    UIDropdownCapability* Dropdown = NULL;
    if (!UIScreen_TryGetWidgetById(Screen, widgetId, &Found) || !TryGetHead(List, &Head, &Dropdown))
    {
        return Error_CreateSuccess();
    }
    DropdownRow* Row = (DropdownRow*)(void*)Found;
    Row->Index = dataIndex;
    Row->IsBound = true;
    GenericBuffer_Clear(&List->Scratch);
    Error Result = UIChoiceCapability_AppendText(&Dropdown->Choice, Head, dataIndex, &List->Scratch);
    return (Result.Code == ErrorCode_Success) ? UITextContent_Set(&Row->Content, List->Scratch._data, NULL) : Result;
}

static Error Row_Unbind(void* owner, uint64_t widgetId)
{
    DropdownList* List = owner;
    Widget* Found = NULL;
    if (UIScreen_TryGetWidgetById(Widget_GetScreen(&List->Base), widgetId, &Found))
    {
        ((DropdownRow*)(void*)Found)->IsBound = false;
    }
    return Error_CreateSuccess();
}


// Fields.
static const UIKindCapability DropdownCapabilities[] =
{
    { .Capability = UIElementCapability_Choice, .Resolve = ResolveChoice },
    { .Capability = UIElementCapability_Dropdown, .Resolve = ResolveDropdownCapability }
};

static const UIKindBehaviour DropdownBehaviour =
{
    .Kind = UIElementKind_Dropdown,
    .Capabilities = DropdownCapabilities,
    .CapabilityCount = sizeof(DropdownCapabilities) / sizeof(DropdownCapabilities[0]),
    .BaseSize = sizeof(UIDropdownLookBase),
    .IsFocusable = true,
    .NeedsEntering = false,
    .EntersItself = false,
    .Construct = Behaviour_Construct,
    .Deconstruct = Behaviour_Deconstruct,
    .Update = Behaviour_Update,
    .OnHoverStart = Behaviour_OnHoverStart,
    .OnMouseInput = Behaviour_OnMouseInput,
    .OnPressLost = Behaviour_OnPressLost,
    .OnScrollInput = Behaviour_OnScrollInput,
    .OnKeyboardInput = Behaviour_OnKeyboardInput
};

static const WidgetVTable ListVTable =
{
    .OnDeconstruct = List_OnDeconstruct,
    .Update = List_Update,
    .Layout = List_Layout,
    .Render = List_Render,
    .OnMouseInput = List_OnMouseInput,
    .OnPressLost = List_OnPressLost,
    .OnScrollInput = List_OnScrollInput,
    .OnKeyboardInput = List_OnKeyboardInput,
    .Measure = List_Measure
};

static const WidgetVTable RowVTable =
{
    .OnDeconstruct = Row_OnDeconstruct,
    .Update = Row_Update,
    .Render = Row_Render,
    .OnHoverStart = Row_OnHoverStart,
    .OnMouseInput = Row_OnMouseInput,
    .Measure = Row_Measure
};

/* The box that cuts the rows: it takes no input itself, so a press between rows reaches the list. */
static const WidgetVTable ClipVTable = { .Update = NULL };

static Error Clip_Construct(void* widget, void* args)
{
    (void)args;
    Error Result = Widget_SetAreSubWidgetsCut(widget, true);
    return (Result.Code == ErrorCode_Success) ? Widget_SetIsInputEnabled(widget, false) : Result; // Only the flags: not initialized yet.
}


// Internal functions (declared in UIKitInternal.h).
const UIKindBehaviour* UIDropdown_GetKindBehaviour(void)
{
    return &DropdownBehaviour;
}


// Public functions: calling code.
Error UIDropdown_Open(UIScreen* screen, uint64_t widgetId)
{
    Widget* Target = NULL;
    UIDropdownCapability* Dropdown = NULL;
    Error Result = ResolveDropdown(screen, widgetId, &Target, &Dropdown);
    return (Result.Code == ErrorCode_Success) ? OpenList(screen, widgetId, GameKey_Unknown) : Result;
}

Error UIDropdown_Close(UIScreen* screen, uint64_t widgetId)
{
    Widget* Target = NULL;
    UIDropdownCapability* Dropdown = NULL;
    Error Result = ResolveDropdown(screen, widgetId, &Target, &Dropdown);
    return (Result.Code == ErrorCode_Success) ? CloseList(screen, Dropdown) : Result;
}

Error UIDropdown_IsOpen(UIScreen* screen, uint64_t widgetId, bool* outIsOpen, uint64_t* outListId)
{
    Widget* Target = NULL;
    UIDropdownCapability* Dropdown = NULL;
    Error Result = ResolveDropdown(screen, widgetId, &Target, &Dropdown);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    *outIsOpen = UIScreen_IsPopupOpen(screen, Dropdown->_listId);
    if (outListId != NULL)
    {
        *outListId = *outIsOpen ? Dropdown->_listId : WIDGET_ID_INVALID;
    }
    return Error_CreateSuccess();
}

Error UIDropdown_SetVisibleCount(UIScreen* screen, uint64_t widgetId, size_t visibleCount)
{
    Widget* Target = NULL;
    UIDropdownCapability* Dropdown = NULL;
    Error Result = ResolveDropdown(screen, widgetId, &Target, &Dropdown);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((visibleCount < 1U) || (visibleCount > UI_DROPDOWN_MAX_VISIBLE_COUNT))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIDropdown_SetVisibleCount: the count must be within [1, UI_DROPDOWN_MAX_VISIBLE_COUNT].");
    }
    Dropdown->_visibleCount = visibleCount;
    return Error_CreateSuccess();
}

Error UIDropdown_SetDirection(UIScreen* screen, uint64_t widgetId, UIDropdownDirection direction)
{
    Widget* Target = NULL;
    UIDropdownCapability* Dropdown = NULL;
    Error Result = ResolveDropdown(screen, widgetId, &Target, &Dropdown);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (((int32_t)direction < (int32_t)UIDropdownDirection_Auto) || ((int32_t)direction > (int32_t)UIDropdownDirection_Down))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIDropdown_SetDirection: an unknown direction.");
    }
    Dropdown->_direction = direction;
    return Error_CreateSuccess();
}

Error UIDropdown_SetIsEnabled(UIScreen* screen, uint64_t widgetId, bool isEnabled)
{
    Widget* Target = NULL;
    UIDropdownCapability* Dropdown = NULL;
    Error Result = ResolveDropdown(screen, widgetId, &Target, &Dropdown);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Dropdown->_isEnabled = isEnabled;
    if (!isEnabled)
    {
        UIPressable_Cancel(&Dropdown->_press);
        Result = CloseList(screen, Dropdown);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    // Closing ran hooks: the dropdown is found again.
    Result = ResolveDropdown(screen, widgetId, &Target, &Dropdown);
    return (Result.Code == ErrorCode_Success) ? Widget_SetIsFocusable(Target, isEnabled) : Result;
}


// Public functions: behaviour helpers.
UIElementState UIDropdownCapability_GetState(const UIDropdownCapability* self, const Widget* widget)
{
    bool IsOpen = UIScreen_IsPopupOpen(Widget_GetScreen(widget), self->_listId);
    return UIElementState_Choose(self->_isEnabled, UIPressable_IsPressed(&self->_press) || IsOpen, false, false, false, Widget_IsHovered(widget),
        Widget_IsFocused(widget));
}

Error UIDropdown_RegisterTypes(UIKit* kit, UIWidgetFactory* factory)
{
    WidgetCapabilityEntry ListCapabilities[] =
    {
        { .CapabilityId = UIKit_GetCapabilityId(kit, UIElementCapability_Scrollable), .Resolver = List_ResolveScrollable }
    };
    WidgetTypeDescriptor List =
    {
        .Name = u8"DropdownList",
        .StructSize = sizeof(DropdownList),
        .VTable = &ListVTable,
        .Constructor = List_Construct,
        .Capabilities = ListCapabilities,
        .CapabilityCount = sizeof(ListCapabilities) / sizeof(ListCapabilities[0]),
        .IsFocusable = true,
        .NeedsEntering = true,
        .EntersItself = false
    };
    WidgetTypeDescriptor Row = { .Name = u8"DropdownRow", .StructSize = sizeof(DropdownRow), .VTable = &RowVTable, .Constructor = Row_Construct };
    WidgetTypeDescriptor Clip = { .Name = u8"Clip", .StructSize = sizeof(Widget), .VTable = &ClipVTable, .Constructor = Clip_Construct };
    Error Result = UIWidgetFactory_RegisterType(factory, &List, &kit->_dropdownListType);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWidgetFactory_RegisterType(factory, &Row, &kit->_dropdownRowType);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWidgetFactory_RegisterType(factory, &Clip, &kit->_clipType);
    }
    return Result;
}
