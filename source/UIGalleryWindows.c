#include <stdio.h>
#include "UIGalleryWindows.h"
#include "NamespacedKey.h"
#include "UIChoice.h"
#include "UIDialog.h"
#include "UIKit.h"
#include "UIMenuBar.h"
#include "UIPopup.h"
#include "UIScreen.h"
#include "UITextBox.h"
#include "UITooltip.h"
#include "UIWindow.h"
#include "UIWindowTypes.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"
#include "wr/WRUserData.h"


// Macros.
/** The Window menu's first item's id: an item's id less it is its type's index. */
#define WINDOW_MENU_FIRST_ID 1000U
/** The most items the Window menu lists. */
#define WINDOW_MENU_CAPACITY ((size_t)32)
/** How many items the scrolling window holds. */
#define SCROLLING_ITEM_COUNT 30
/** The notes' dialog's answers. */
#define ANSWER_CLOSE 1
#define ANSWER_KEEP 2
/** Room for a report. */
#define REPORT_CAPACITY ((size_t)160)


// Types.
/* One of the page's window types. */
typedef struct GalleryWindowTypeStruct
{
    const unsigned char* Key;
    const unsigned char* Name;
    uint32_t Letter;
    UIWindowFit Across;
    UIWindowFit Down;
    Vector2 Size;
    bool IsSingle;
    UIWindowTypeFill Fill;
    /* Where it first opens, in the screen's UI pixels. */
    Vector2 Place;
} GalleryWindowType;


// Static functions: helpers.
static void Report(GalleryWindows* self, const char* text)
{
    if (self->Report != NULL)
    {
        Error Result = self->Report(self->ReportContext, text);
        Error_Deconstruct(&Result); // Only a status line.
    }
}

static Error AddLabel(UIKit* kit, UIScreen* screen, uint64_t parentId, const unsigned char* text, const unsigned char* variant, bool isWrapped)
{
    UILabelOptions Options = UILabelOptions_Create(text);
    Options.Element.Variant = variant;
    Options.IsWrapped = isWrapped;
    uint64_t Id = WIDGET_ID_INVALID;
    return UIKit_CreateLabel(kit, screen, parentId, &Options, &Id);
}

static Error AddLabelledRow(UIKit* kit, UIScreen* screen, uint64_t formId, const unsigned char* text, uint64_t* outRowId)
{
    UILabelledOptions Options = UILabelledOptions_Create(text);
    return UIKit_CreateLabelled(kit, screen, formId, &Options, outRowId);
}


// Static functions: what fills each window.
/* Both ways fit: a form of labelled rows, which the window never gets smaller than. */
static Error FillFitting(UIScreen* screen, uint64_t windowId, uint64_t contentId, const UserData* userData)
{
    (void)windowId;
    (void)userData;
    UIKit* Kit = UIKit_FromScreen(screen);
    Error Result = AddLabel(Kit, screen, contentId, u8"Both ways fit: it gets no smaller than what it holds.", u8"hint", true);
    UIFormOptions FormOptions = UIFormOptions_Create();
    uint64_t Form = WIDGET_ID_INVALID;
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIKit_CreateForm(Kit, screen, contentId, &FormOptions, &Form);
    }
    uint64_t Row = WIDGET_ID_INVALID;
    uint64_t Id = WIDGET_ID_INVALID;
    if (Result.Code == ErrorCode_Success)
    {
        Result = AddLabelledRow(Kit, screen, Form, u8"Name", &Row);
    }
    if (Result.Code == ErrorCode_Success)
    {
        UITextBoxOptions Text = UITextBoxOptions_Create();
        Text.Text = u8"Lamp post";
        Result = UIKit_CreateTextBox(Kit, screen, Row, &Text, &Id);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = AddLabelledRow(Kit, screen, Form, u8"Brightness", &Row);
    }
    if (Result.Code == ErrorCode_Success)
    {
        UISliderOptions Slider = UISliderOptions_Create();
        Slider.Value = UIValueOptions_CreateRange(0.0, 1.0, 0.6);
        Result = UIKit_CreateSlider(Kit, screen, Row, &Slider, &Id);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = AddLabelledRow(Kit, screen, Form, u8"Casts shadows", &Row);
    }
    if (Result.Code == ErrorCode_Success)
    {
        UIToggleOptions Toggle = UIToggleOptions_Create(u8"on");
        Result = UIKit_CreateToggle(Kit, screen, Row, &Toggle, &Id);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = AddLabelledRow(Kit, screen, Form, u8"Size", &Row);
    }
    if (Result.Code == ErrorCode_Success)
    {
        UIDropdownOptions Dropdown = UIDropdownOptions_Create();
        Result = UIKit_CreateDropdown(Kit, screen, Row, &Dropdown, &Id);
    }
    static const unsigned char* const Sizes[] = { u8"Small", u8"Medium", u8"Large" };
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIChoice_SetOptions(screen, Id, Sizes, sizeof(Sizes) / sizeof(Sizes[0]));
    }
    return (Result.Code == ErrorCode_Success) ? UIChoice_SetSelected(screen, Id, true, 1U) : Result;
}

/* Across fits, down scrolls: a long column of buttons, with the bar while it is longer than the window. */
static Error FillScrolling(UIScreen* screen, uint64_t windowId, uint64_t contentId, const UserData* userData)
{
    (void)windowId;
    (void)userData;
    UIKit* Kit = UIKit_FromScreen(screen);
    Error Result = AddLabel(Kit, screen, contentId, u8"Across fits, down scrolls: the bar shows while it holds more than shows.", u8"hint",
        true);
    for (int32_t Item = 1; (Item <= SCROLLING_ITEM_COUNT) && (Result.Code == ErrorCode_Success); Item++)
    {
        unsigned char Text[32];
        snprintf((char*)Text, sizeof(Text), "Item %d", (int)Item);
        UIButtonOptions Button = UIButtonOptions_Create(Text);
        uint64_t Id = WIDGET_ID_INVALID;
        Result = UIKit_CreateButton(Kit, screen, contentId, &Button, &Id);
    }
    return Result;
}

/* A fixed window, as a screen in the world is: no edges, no maximize, its contents at a scale of their own. */
static Error FillFixed(UIScreen* screen, uint64_t windowId, uint64_t contentId, const UserData* userData)
{
    (void)userData;
    UIKit* Kit = UIKit_FromScreen(screen);
    Error Result = UIElement_SetScale(screen, windowId, 1.25f);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWindow_SetIsResizable(screen, windowId, false);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWindow_SetButtons(screen, windowId, true, false, true);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = AddLabel(Kit, screen, contentId, u8"Fixed, as a screen in the world is: no edges, no maximize, all of it at a scale of 1.25.",
            u8"hint", true);
    }
    uint64_t Id = WIDGET_ID_INVALID;
    if (Result.Code == ErrorCode_Success)
    {
        UIButtonOptions Button = UIButtonOptions_Create(u8"Clear");
        Result = UIKit_CreateButton(Kit, screen, contentId, &Button, &Id);
    }
    if (Result.Code == ErrorCode_Success)
    {
        UISliderOptions Slider = UISliderOptions_Create();
        Slider.Value = UIValueOptions_CreateRange(0.0, 1.0, 0.8);
        Result = UIKit_CreateSlider(Kit, screen, contentId, &Slider, &Id);
    }
    return Result;
}

/* The notes' dialog was answered: Close closes them, Keep keeps them. */
static Error OnNotesAnswer(UIScreen* screen, const UIEvent* event, const UserData* userData)
{
    GalleryWindows* Self = UserData_GetPointer(userData);
    if ((event->WidgetEventId != (int32_t)UIElementEvent_Activated) || !event->HasIndex || (event->WidgetId != Self->AskingDialogId))
    {
        return Error_CreateSuccess();
    }
    uint64_t WindowId = Self->AskingWindowId;
    Self->AskingDialogId = WIDGET_ID_INVALID;
    Self->AskingWindowId = WIDGET_ID_INVALID;
    if ((event->Index != ANSWER_CLOSE) || !UIScreen_IsWidgetAlive(screen, WindowId))
    {
        Report(Self, "the notes stay open");
        return Error_CreateSuccess();
    }
    Report(Self, "the notes closed: what they said is gone");
    return UIWindow_CloseNow(screen, WindowId);
}

/* The notes ask before closing while they say something: a dialog, and the window closes on its answer. */
static bool AskBeforeClosing(UIScreen* screen, uint64_t windowId, const UserData* userData)
{
    GalleryWindows* Self = UserData_GetPointer(userData);
    uint64_t ContentId = WIDGET_ID_INVALID;
    Widget* Content = NULL;
    const unsigned char* Text = NULL;
    Error Result = UIWindow_GetContentId(screen, windowId, &ContentId);
    if ((Result.Code == ErrorCode_Success) && UIScreen_TryGetWidgetById(screen, ContentId, &Content) && (Widget_GetSubWidgetCount(Content) > 0U))
    {
        Result = UITextBox_GetText(screen, Widget_GetId(Widget_GetSubWidgetAt(Content, 0U)), &Text, NULL);
    }
    Error_Deconstruct(&Result); // Notes that cannot say what they hold have nothing to lose.
    if ((Text == NULL) || (Text[0] == 0U))
    {
        Report(Self, "the notes closed: they said nothing");
        return true;
    }
    if ((Self->AskingDialogId != WIDGET_ID_INVALID) && UIScreen_IsPopupOpen(screen, Self->AskingDialogId))
    {
        return false; // One question at a time.
    }
    // A question left unanswered (its dialog closed with the page) is asked again.
    Self->AskingDialogId = WIDGET_ID_INVALID;
    Self->AskingWindowId = WIDGET_ID_INVALID;
    static const UIDialogButton Buttons[] =
    {
        { .Id = ANSWER_CLOSE, .Text = u8"Close", .IsDefault = true, .IsCancel = false },
        { .Id = ANSWER_KEEP, .Text = u8"Keep", .IsDefault = false, .IsCancel = true },
    };
    UIDialogOptions Options = UIDialogOptions_Create(u8"Close these notes?");
    Options.Message = u8"What they say is lost.";
    Options.Buttons = Buttons;
    Options.ButtonCount = sizeof(Buttons) / sizeof(Buttons[0]);
    Options.OwnerId = windowId;
    uint64_t DialogId = WIDGET_ID_INVALID;
    Result = UIKit_CreateDialog(UIKit_FromScreen(screen), screen, &Options, &DialogId);
    UserData Data = UserData_FromPointer(Self);
    uint64_t Subscription = UI_SCREEN_ID_INVALID;
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIScreen_Subscribe(screen, DialogId, UI_EVENT_KIND_MASK(UIEventKind_Widget), OnNotesAnswer, &Data, &Subscription);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIDialog_Open(screen, DialogId, WIDGET_ID_INVALID);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        Report(Self, "the notes could not ask: they stay open");
        return false;
    }
    Self->AskingDialogId = DialogId;
    Self->AskingWindowId = windowId;
    Report(Self, "the notes ask before closing");
    return false;
}

/* Notes: a text box filling the window, and a close that asks first while it says something. */
static Error FillNotes(UIScreen* screen, uint64_t windowId, uint64_t contentId, const UserData* userData)
{
    UIKit* Kit = UIKit_FromScreen(screen);
    UITextBoxOptions Text = UITextBoxOptions_CreateMultiline();
    Text.Placeholder = u8"Type here: closing asks first.";
    Text.Element.Hint = UIStackHint_Fill(1.0f);
    uint64_t Id = WIDGET_ID_INVALID;
    Error Result = UIKit_CreateTextBox(Kit, screen, contentId, &Text, &Id);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWindow_SetCloseQuery(screen, windowId, AskBeforeClosing, userData);
    }
    return (Result.Code == ErrorCode_Success) ? UIWindow_SetDefaultFocus(screen, windowId, Id, true) : Result;
}


// Fields.
static const GalleryWindowType Types[GALLERY_WINDOW_TYPE_COUNT] =
{
    { .Key = u8"tbp:gallery_fitting", .Name = u8"Fitting", .Letter = (uint32_t)'F', .Across = UIWindowFit_Fit, .Down = UIWindowFit_Fit,
        .Size = { 340.0f, 210.0f }, .IsSingle = true, .Fill = FillFitting, .Place = { 40.0f, 200.0f } },
    { .Key = u8"tbp:gallery_scrolling", .Name = u8"Scrolling", .Letter = (uint32_t)'S', .Across = UIWindowFit_Fit, .Down = UIWindowFit_Scroll,
        .Size = { 280.0f, 260.0f }, .IsSingle = true, .Fill = FillScrolling, .Place = { 410.0f, 200.0f } },
    { .Key = u8"tbp:gallery_fixed", .Name = u8"Fixed", .Letter = (uint32_t)'X', .Across = UIWindowFit_Fit, .Down = UIWindowFit_Fit,
        .Size = { 340.0f, 220.0f }, .IsSingle = true, .Fill = FillFixed, .Place = { 720.0f, 200.0f } },
    { .Key = u8"tbp:gallery_notes", .Name = u8"Notes", .Letter = (uint32_t)'N', .Across = UIWindowFit_Fit, .Down = UIWindowFit_Fit,
        .Size = { 360.0f, 180.0f }, .IsSingle = false, .Fill = FillNotes, .Place = { 40.0f, 440.0f } },
};


// Static functions: the page.
/* Whether a type index is one of the page's. */
static bool IsOwnType(const GalleryWindows* self, uint32_t index)
{
    for (size_t Slot = 0U; self->IsRegistered && (Slot < GALLERY_WINDOW_TYPE_COUNT); Slot++)
    {
        if (self->TypeIndices[Slot] == index)
        {
            return true;
        }
    }
    return false;
}

/* Whether a widget is a window of one of the page's types (the gallery's other pages have windows of their own). */
static bool IsOwnWindow(GalleryWindows* self, uint64_t widgetId)
{
    UIWindowTypes* WindowTypes = NULL;
    uint32_t Index = 0U;
    Error Result = UIWindow_GetType(self->Screen, widgetId, &WindowTypes, &Index);
    bool IsOwn = (Result.Code == ErrorCode_Success) && (WindowTypes == self->Types) && (WindowTypes != NULL) && IsOwnType(self, Index);
    Error_Deconstruct(&Result); // No window, or one of no type: not the page's.
    return IsOwn;
}

/* Opens one of the page's windows from the Window menu. */
static Error OnWindowMenu(UIScreen* screen, const UIMenuActivation* activation, const UserData* userData)
{
    GalleryWindows* Self = UserData_GetPointer(userData);
    if ((Self->Types == NULL) || (activation->ItemId < WINDOW_MENU_FIRST_ID) || !IsOwnType(Self, activation->ItemId - WINDOW_MENU_FIRST_ID))
    {
        return Error_CreateSuccess();
    }
    uint32_t Index = activation->ItemId - WINDOW_MENU_FIRST_ID;
    uint64_t WindowId = WIDGET_ID_INVALID;
    const UIWindowType* Type = UIWindowTypes_Get(Self->Types, Index);
    char Name[REPORT_CAPACITY / 2U];
    snprintf(Name, sizeof(Name), "%s", (Type != NULL) ? (const char*)Type->_displayName : "a window");
    // What cannot open is told, never handed on: a menu handler's error would stop the screen's update.
    Error Result = UIWindowTypes_OpenAt(Self->Types, screen, Index, &WindowId);
    char Text[REPORT_CAPACITY];
    if (Result.Code != ErrorCode_Success)
    {
        snprintf(Text, sizeof(Text), "the Window menu could not open %s: %s", Name,
            (Result.Message != NULL) ? (const char*)Result.Message : "no reason given");
    }
    else
    {
        snprintf(Text, sizeof(Text), (WindowId != WIDGET_ID_INVALID) ? "the Window menu opened %s" : "the Window menu opened %s, and it closed", Name);
    }
    Error_Deconstruct(&Result);
    Report(Self, Text);
    return Error_CreateSuccess();
}

/* Opens the page's windows where they first stand. */
static Error OpenAll(GalleryWindows* self)
{
    Error Result = Error_CreateSuccess();
    for (size_t Index = 0U; (Index < GALLERY_WINDOW_TYPE_COUNT) && self->IsRegistered && (Result.Code == ErrorCode_Success); Index++)
    {
        uint64_t WindowId = WIDGET_ID_INVALID;
        Result = UIWindowTypes_OpenAt(self->Types, self->Screen, self->TypeIndices[Index], &WindowId);
        if (Result.Code == ErrorCode_Success)
        {
            Rectangle Box = { .x = Types[Index].Place.x, .y = Types[Index].Place.y, .width = Types[Index].Size.x, .height = Types[Index].Size.y };
            Result = UIWindow_SetBox(self->Screen, WindowId, Box);
        }
    }
    self->HasOpened = true;
    // Nothing in them keeps the focus (the notes would take N as typing): a press chooses where it goes.
    return (Result.Code == ErrorCode_Success) ? UIScreen_SetFocusedWidget(self->Screen, NULL) : Result;
}


// Public functions.
Error GalleryWindows_Construct(GalleryWindows* self, UIWindowTypes* types, KeyRegistry* keys, UIKit* kit, UIScreen* screen,
    GalleryWindowsReport report, void* reportContext)
{
    Memory_Zero(self, sizeof(*self));
    self->Types = types;
    self->Kit = kit;
    self->Screen = screen;
    self->Report = report;
    self->ReportContext = reportContext;
    if ((types == NULL) || (keys == NULL))
    {
        return Error_CreateSuccess();
    }
    Error Result = KeyRegistry_Intern(keys, u8"tbp:ui_gallery", &self->SourceKeyId);
    for (size_t Index = 0U; (Index < GALLERY_WINDOW_TYPE_COUNT) && (Result.Code == ErrorCode_Success); Index++)
    {
        UIWindowTypeDefinition Definition = UIWindowTypeDefinition_Create(Types[Index].Name, Types[Index].Fill);
        Definition.Icon = UIElementIcon_FromLetter(Types[Index].Letter);
        Definition.Data = UserData_FromPointer(self);
        Definition.FitAcross = Types[Index].Across;
        Definition.FitDown = Types[Index].Down;
        Definition.DefaultSize = Types[Index].Size;
        Definition.IsSingle = Types[Index].IsSingle;
        Result = UIWindowTypes_Register(types, Types[Index].Key, &Definition, self->SourceKeyId, &self->TypeIndices[Index]);
    }
    self->IsRegistered = Result.Code == ErrorCode_Success;
    if (!self->IsRegistered)
    {
        (void)UIWindowTypes_WithdrawSource(types, self->SourceKeyId);
    }
    return Result;
}

void GalleryWindows_Deconstruct(GalleryWindows* self)
{
    if ((self->Types != NULL) && self->IsRegistered)
    {
        (void)UIWindowTypes_WithdrawSource(self->Types, self->SourceKeyId);
    }
    self->IsRegistered = false;
}

Error GalleryWindows_BuildPage(GalleryWindows* self, uint64_t columnId)
{
    Error Result = AddLabel(self->Kit, self->Screen, columnId, u8"UI gallery: windows", u8"heading", false);
    if (Result.Code == ErrorCode_Success)
    {
        Result = AddLabel(self->Kit, self->Screen, columnId, u8"N the next page. Drag a window by its title bar; resize it by its edges and "
            u8"corners; double click its title bar to maximize it. Its buttons minimize, maximize and close it, and the Window menu opens it "
            u8"again where it closed. Type in one, click another, then the first one's title bar: the typing goes on, and nothing in it was "
            u8"clicked. Shrink the game's window: they stay where they can be reached.", u8"hint", true);
    }
    if ((Result.Code == ErrorCode_Success) && (self->Types == NULL))
    {
        return AddLabel(self->Kit, self->Screen, columnId, u8"The program has no window types: there are no windows to show.", NULL, false);
    }
    UIMenuBarOptions Options = UIMenuBarOptions_Create();
    Options.Handler = OnWindowMenu;
    Options.Data = UserData_FromPointer(self);
    uint64_t BarId = WIDGET_ID_INVALID;
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIKit_CreateMenuBar(self->Kit, self->Screen, columnId, &Options, &BarId);
        self->MenuBarId = BarId;
    }
    UIMenuItem Items[WINDOW_MENU_CAPACITY];
    size_t Count = 0U;
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWindowTypes_WriteMenuItems(self->Types, WINDOW_MENU_FIRST_ID, Items, WINDOW_MENU_CAPACITY, &Count);
    }
    // The page's types alone: the workspace page opens its own in its workspace.
    size_t Kept = 0U;
    for (size_t Item = 0U; Item < Count; Item++)
    {
        if ((Items[Item].Id >= WINDOW_MENU_FIRST_ID) && IsOwnType(self, Items[Item].Id - WINDOW_MENU_FIRST_ID))
        {
            Items[Kept] = Items[Item];
            Kept++;
        }
    }
    Count = Kept;
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIMenuBar_AddEntry(self->Screen, BarId, u8"Window", Items, Count, NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UITooltip_Set(self->Screen, BarId, u8"Window opens each of the page's window types; one that may be open once comes to the front.");
    }
    return Result;
}

Error GalleryWindows_SetShown(GalleryWindows* self, bool isShown)
{
    Error Result = (isShown && !self->HasOpened) ? OpenAll(self) : Error_CreateSuccess();
    for (size_t Index = 0U; (Index < UIScreen_GetRootCount(self->Screen)) && (Result.Code == ErrorCode_Success); Index++)
    {
        Widget* Root = UIScreen_GetRootAt(self->Screen, Index);
        if ((Root != NULL) && IsOwnWindow(self, Widget_GetId(Root)))
        {
            Result = Widget_SetIsRendered(Root, isShown);
        }
    }
    if ((Result.Code == ErrorCode_Success) && !isShown)
    {
        Result = UIScreen_SetWorkspaceInsets(self->Screen, (UIInsets){ .Left = 0.0f, .Top = 0.0f, .Right = 0.0f, .Bottom = 0.0f });
    }
    return Result;
}

Error GalleryWindows_KeepWorkspace(GalleryWindows* self, uint64_t bottomLineId)
{
    Widget* Bar = NULL;
    Widget* Line = NULL;
    float Scale = UIScreen_GetScale(self->Screen);
    if (!UIScreen_TryGetWidgetById(self->Screen, self->MenuBarId, &Bar) || !UIScreen_TryGetWidgetById(self->Screen, bottomLineId, &Line)
        || !(Scale > 0.0f))
    {
        return Error_CreateSuccess();
    }
    Rectangle BarBox = UIScreen_GetWidgetPixelBox(self->Screen, Bar);
    Rectangle LineBox = UIScreen_GetWidgetPixelBox(self->Screen, Line);
    UIViewport Viewport = UIScreen_GetViewport(self->Screen);
    UIInsets Wanted =
    {
        .Left = 0.0f,
        .Top = Math_ClampFloat((BarBox.y + BarBox.height) / Scale, 0.0f, UI_LAYOUT_MAX_LENGTH),
        .Right = 0.0f,
        .Bottom = Math_ClampFloat((Viewport.SizePixels.y - LineBox.y) / Scale, 0.0f, UI_LAYOUT_MAX_LENGTH)
    };
    UIInsets Current = UIScreen_GetWorkspaceInsets(self->Screen);
    if (Memory_IsEqual(&Current, &Wanted, sizeof(Wanted)))
    {
        return Error_CreateSuccess();
    }
    return UIScreen_SetWorkspaceInsets(self->Screen, Wanted);
}
