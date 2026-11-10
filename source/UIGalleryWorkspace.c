#include <stdio.h>
#include <string.h>
#include "UIGalleryWorkspace.h"
#include "GameInput.h"
#include "NamespacedKey.h"
#include "UIRenderView.h"
#include "UIChoice.h"
#include "UIDialog.h"
#include "UIKit.h"
#include "UIMenuBar.h"
#include "UIScreen.h"
#include "UIStatusBar.h"
#include "UITextBox.h"
#include "UITooltip.h"
#include "UIWindow.h"
#include "UIWindowTypes.h"
#include "UIWorkspace.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"
#include "wr/WRPath.h"
#include "wr/WRUserData.h"
#include "raylib/rlgl.h"
#include <math.h>


// Macros.
/** The Window menu's first item's id: an item's id less it is the type's slot in Types. */
#define WINDOW_MENU_FIRST_ID 2000U
/** The Layout menu's items. */
#define LAYOUT_MENU_NEW 3001U
#define LAYOUT_MENU_RENAME 3002U
#define LAYOUT_MENU_DELETE 3003U
#define LAYOUT_MENU_RESET 3004U
/** The rename dialog's answers. */
#define ANSWER_RENAME 1
#define ANSWER_CANCEL 2
/** Room for a report. */
#define REPORT_CAPACITY ((size_t)192)
/** How many entries the outline lists, and how many lines the log holds. */
#define OUTLINE_ENTRY_COUNT 24
#define LOG_LINE_COUNT 40
/** Room for the log's text. */
#define LOG_TEXT_CAPACITY ((size_t)4096)
/** The layouts file's path's first capacity. */
#define PATH_INITIAL_CAPACITY ((size_t)256)
/** The default layout's shares: the outline of the whole width, the inspector of what the outline leaves, the log of the view's column. */
#define OUTLINE_SHARE 0.2f
#define INSPECTOR_SHARE 0.3f
#define LOG_SHARE 0.3f
/** The picture's stripes: their width, their speed at first and its limits (pixels a second), and how much a wheel notch changes it. */
#define STRIPE_WIDTH 24
#define PICTURE_START_SPEED 60.0f
#define PICTURE_MIN_SPEED 5.0f
#define PICTURE_MAX_SPEED 2000.0f
#define PICTURE_WHEEL_FACTOR 1.25f
/** The picture's grid spacing and the orbiting dot's radius and period, in pixels and seconds. */
#define GRID_SPACING 64
#define ORBIT_PERIOD_SECONDS 4.0
#define DOT_RADIUS 10.0f


// Types.
/* One of the page's window types. */
typedef struct GalleryDockTypeStruct
{
    const unsigned char* Key;
    const unsigned char* Name;
    uint32_t Letter;
    UIWindowFit Across;
    UIWindowFit Down;
    Vector2 Size;
    bool IsSingle;
    UIWindowTypeFill Fill;
} GalleryDockType;

/* The type slots, in Types' order. */
typedef enum GalleryDockSlotEnum
{
    GalleryDockSlot_Outline,
    GalleryDockSlot_View,
    GalleryDockSlot_Inspector,
    GalleryDockSlot_Log,
    GalleryDockSlot_Notes
} GalleryDockSlot;


// Static functions: helpers.
static void Report(GalleryWorkspace* self, const char* text)
{
    if (self->Report != NULL)
    {
        Error Result = self->Report(self->ReportContext, text);
        Error_Deconstruct(&Result); // Only a log line and a status.
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
/* The outline: what a world holds, a button a thing, longer than it shows. */
static Error FillOutline(UIScreen* screen, uint64_t windowId, uint64_t contentId, const UserData* userData)
{
    (void)windowId;
    (void)userData;
    static const char* const Kinds[] = { "Terrain", "Road", "Path", "House", "Lamp", "Bench", "Tree", "Hedge" };
    UIKit* Kit = UIKit_FromScreen(screen);
    Error Result = AddLabel(Kit, screen, contentId, u8"What the world holds.", u8"hint", true);
    for (int32_t Entry = 0; (Entry < OUTLINE_ENTRY_COUNT) && (Result.Code == ErrorCode_Success); Entry++)
    {
        unsigned char Text[32];
        size_t Kind = (size_t)Entry % (sizeof(Kinds) / sizeof(Kinds[0]));
        snprintf((char*)Text, sizeof(Text), "%s %d", Kinds[Kind], (int)((Entry / (int32_t)(sizeof(Kinds) / sizeof(Kinds[0]))) + 1));
        UIButtonOptions Button = UIButtonOptions_Create(Text);
        uint64_t Id = WIDGET_ID_INVALID;
        Result = UIKit_CreateButton(Kit, screen, contentId, &Button, &Id);
    }
    return Result;
}

/* The view: what to try, and a render view the gallery draws a moving pattern into, as the editor draws its world. */
static Error FillView(UIScreen* screen, uint64_t windowId, uint64_t contentId, const UserData* userData)
{
    GalleryWorkspace* Self = UserData_GetPointer(userData);
    UIKit* Kit = UIKit_FromScreen(screen);
    Error Result = AddLabel(Kit, screen, contentId, u8"N the next page. Drag a tab out of its strip: its window floats under the pointer. Let it go "
        u8"on a guide to dock it there, on a tab strip to add a tab, anywhere else to leave it floating; Escape mid-drag puts it back. Drag the "
        u8"splitters, double click a tab to maximize its window, right click it to undock it. The Layout menu keeps arrangements (in "
        u8"ui_gallery_layouts.json next to the game).", u8"hint", true);
    if (Result.Code == ErrorCode_Success)
    {
        Result = AddLabel(Kit, screen, contentId, u8"The picture is a render view: the gallery draws it at the view's size each frame, also while "
            u8"you drag an edge or a splitter. A press enters it (it keeps Tab, the arrows and Enter for its host; Escape lets go of it); the left "
            u8"button puts the marker, the right button pans with the cursor hidden, the wheel changes the stripes' speed.", u8"hint", true);
    }
    UIRenderViewOptions Options = UIRenderViewOptions_Create();
    Options.Element.Hint = UIStackHint_Fill(1.0f);
    Options.CapturedButtons = UI_BUTTON_BIT(GameKey_MouseRight);
    uint64_t ViewId = WIDGET_ID_INVALID;
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIKit_CreateRenderView(Kit, screen, contentId, &Options, &ViewId);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWindow_SetDefaultFocus(screen, windowId, ViewId, true);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Self->Picture.ViewId = ViewId;
    }
    return Result;
}

/* The inspector: a form of the values a thing has. */
static Error FillInspector(UIScreen* screen, uint64_t windowId, uint64_t contentId, const UserData* userData)
{
    (void)windowId;
    (void)userData;
    UIKit* Kit = UIKit_FromScreen(screen);
    UIFormOptions FormOptions = UIFormOptions_Create();
    uint64_t Form = WIDGET_ID_INVALID;
    Error Result = UIKit_CreateForm(Kit, screen, contentId, &FormOptions, &Form);
    uint64_t Row = WIDGET_ID_INVALID;
    uint64_t Id = WIDGET_ID_INVALID;
    if (Result.Code == ErrorCode_Success)
    {
        Result = AddLabelledRow(Kit, screen, Form, u8"Name", &Row);
    }
    if (Result.Code == ErrorCode_Success)
    {
        UITextBoxOptions Text = UITextBoxOptions_Create();
        Text.Text = u8"Bench 1";
        Result = UIKit_CreateTextBox(Kit, screen, Row, &Text, &Id);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = AddLabelledRow(Kit, screen, Form, u8"Weathering", &Row);
    }
    if (Result.Code == ErrorCode_Success)
    {
        UISliderOptions Slider = UISliderOptions_Create();
        Slider.Value = UIValueOptions_CreateRange(0.0, 1.0, 0.35);
        Result = UIKit_CreateSlider(Kit, screen, Row, &Slider, &Id);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = AddLabelledRow(Kit, screen, Form, u8"Can be sat on", &Row);
    }
    if (Result.Code == ErrorCode_Success)
    {
        UIToggleOptions Toggle = UIToggleOptions_Create(u8"yes");
        Toggle.State = UIToggleState_On;
        Result = UIKit_CreateToggle(Kit, screen, Row, &Toggle, &Id);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = AddLabelledRow(Kit, screen, Form, u8"Material", &Row);
    }
    if (Result.Code == ErrorCode_Success)
    {
        UIDropdownOptions Dropdown = UIDropdownOptions_Create();
        Result = UIKit_CreateDropdown(Kit, screen, Row, &Dropdown, &Id);
    }
    static const unsigned char* const Materials[] = { u8"Oak", u8"Pine", u8"Iron", u8"Stone" };
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIChoice_SetOptions(screen, Id, Materials, sizeof(Materials) / sizeof(Materials[0]));
    }
    return (Result.Code == ErrorCode_Success) ? UIChoice_SetSelected(screen, Id, true, 0U) : Result;
}

/* The log: lines of what happened, to read and select. */
static Error FillLog(UIScreen* screen, uint64_t windowId, uint64_t contentId, const UserData* userData)
{
    (void)windowId;
    (void)userData;
    static const char* const Events[] = { "terrain chunk loaded", "path rebuilt", "prefab placed", "lamp switched on", "save written" };
    char* Text = Memory_Allocate(LOG_TEXT_CAPACITY);
    size_t Length = 0U;
    Text[0] = 0;
    for (int32_t Line = 0; (Line < LOG_LINE_COUNT) && (Length < LOG_TEXT_CAPACITY); Line++)
    {
        int Written = snprintf(Text + Length, LOG_TEXT_CAPACITY - Length, "%s08:%02d:%02d %s", (Line > 0) ? "\n" : "", (int)(Line / 6),
            (int)((Line * 7) % 60), Events[(size_t)Line % (sizeof(Events) / sizeof(Events[0]))]);
        size_t Grown = (Written > 0) ? (Length + (size_t)Written) : Length;
        Length = (Grown < LOG_TEXT_CAPACITY) ? Grown : (LOG_TEXT_CAPACITY - 1U);
    }
    UITextBoxOptions Options = UITextBoxOptions_CreateMultiline();
    Options.Text = (const unsigned char*)Text;
    Options.IsEditable = false;
    Options.IsWrapped = false;
    Options.Element.Hint = UIStackHint_Fill(1.0f);
    uint64_t Id = WIDGET_ID_INVALID;
    Error Result = UIKit_CreateTextBox(UIKit_FromScreen(screen), screen, contentId, &Options, &Id);
    Memory_Free(Text);
    return Result;
}

/* Notes: a text box filling the window; what is typed stays with the window wherever it goes. */
static Error FillNotes(UIScreen* screen, uint64_t windowId, uint64_t contentId, const UserData* userData)
{
    (void)userData;
    UITextBoxOptions Text = UITextBoxOptions_CreateMultiline();
    Text.Placeholder = u8"Type here: it stays with the window, docked, floating or in another layout.";
    Text.Element.Hint = UIStackHint_Fill(1.0f);
    uint64_t Id = WIDGET_ID_INVALID;
    Error Result = UIKit_CreateTextBox(UIKit_FromScreen(screen), screen, contentId, &Text, &Id);
    return (Result.Code == ErrorCode_Success) ? UIWindow_SetDefaultFocus(screen, windowId, Id, true) : Result;
}


// Fields.
static const GalleryDockType Types[GALLERY_WORKSPACE_TYPE_COUNT] =
{
    [GalleryDockSlot_Outline] = { .Key = u8"tbp:gallery_dock_outline", .Name = u8"Outline", .Letter = (uint32_t)'O', .Across = UIWindowFit_Fit,
        .Down = UIWindowFit_Scroll, .Size = { 220.0f, 320.0f }, .IsSingle = true, .Fill = FillOutline },
    [GalleryDockSlot_View] = { .Key = u8"tbp:gallery_dock_view", .Name = u8"View", .Letter = (uint32_t)'V', .Across = UIWindowFit_Fit,
        .Down = UIWindowFit_Fit, .Size = { 480.0f, 360.0f }, .IsSingle = true, .Fill = FillView },
    [GalleryDockSlot_Inspector] = { .Key = u8"tbp:gallery_dock_inspector", .Name = u8"Inspector", .Letter = (uint32_t)'I',
        .Across = UIWindowFit_Fit, .Down = UIWindowFit_Scroll, .Size = { 300.0f, 240.0f }, .IsSingle = true, .Fill = FillInspector },
    [GalleryDockSlot_Log] = { .Key = u8"tbp:gallery_dock_log", .Name = u8"Log", .Letter = (uint32_t)'L', .Across = UIWindowFit_Scroll,
        .Down = UIWindowFit_Scroll, .Size = { 420.0f, 180.0f }, .IsSingle = true, .Fill = FillLog },
    [GalleryDockSlot_Notes] = { .Key = u8"tbp:gallery_dock_notes", .Name = u8"Notes", .Letter = (uint32_t)'N', .Across = UIWindowFit_Scroll,
        .Down = UIWindowFit_Scroll, .Size = { 320.0f, 200.0f }, .IsSingle = false, .Fill = FillNotes },
};


// Static functions: the default layout.
/* The view in the middle, the outline at its left, the inspector at its right, the log under it with the notes as a second tab. */
static Error BuildDefault(UIScreen* screen, uint64_t workspaceId, const UserData* userData)
{
    GalleryWorkspace* Self = UserData_GetPointer(userData);
    uint64_t Windows[GALLERY_WORKSPACE_TYPE_COUNT];
    Error Result = Error_CreateSuccess();
    for (size_t Slot = 0U; (Slot < GALLERY_WORKSPACE_TYPE_COUNT) && (Result.Code == ErrorCode_Success); Slot++)
    {
        Windows[Slot] = WIDGET_ID_INVALID;
        Result = UIWorkspace_OpenType(screen, workspaceId, Self->TypeIndices[Slot], &Windows[Slot]);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWorkspace_Dock(screen, workspaceId, Windows[GalleryDockSlot_View], WIDGET_ID_INVALID, UIDockZone_Center);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWorkspace_Dock(screen, workspaceId, Windows[GalleryDockSlot_Outline], Windows[GalleryDockSlot_View], UIDockZone_Left);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWorkspace_Dock(screen, workspaceId, Windows[GalleryDockSlot_Inspector], Windows[GalleryDockSlot_View], UIDockZone_Right);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWorkspace_Dock(screen, workspaceId, Windows[GalleryDockSlot_Log], Windows[GalleryDockSlot_View], UIDockZone_Bottom);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWorkspace_Dock(screen, workspaceId, Windows[GalleryDockSlot_Notes], Windows[GalleryDockSlot_Log], UIDockZone_Center);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWorkspace_SelectTab(screen, workspaceId, Windows[GalleryDockSlot_Log]);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWorkspace_SetAreaShare(screen, workspaceId, Windows[GalleryDockSlot_Outline], OUTLINE_SHARE);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWorkspace_SetAreaShare(screen, workspaceId, Windows[GalleryDockSlot_Inspector], INSPECTOR_SHARE);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWorkspace_SetAreaShare(screen, workspaceId, Windows[GalleryDockSlot_Log], LOG_SHARE);
    }
    // Nothing keeps the focus (the notes would take N as typing): a press chooses where it goes.
    return (Result.Code == ErrorCode_Success) ? UIScreen_SetFocusedWidget(screen, NULL) : Result;
}


// Static functions: the menus.
/* The rename dialog was answered: Rename names the active layout what its box says, if it can. */
static Error OnRenameAnswer(UIScreen* screen, const UIEvent* event, const UserData* userData)
{
    GalleryWorkspace* Self = UserData_GetPointer(userData);
    if ((event->WidgetEventId != (int32_t)UIElementEvent_Activated) || !event->HasIndex || (event->WidgetId != Self->RenameDialogId))
    {
        return Error_CreateSuccess();
    }
    uint64_t BoxId = Self->RenameBoxId;
    Self->RenameDialogId = WIDGET_ID_INVALID;
    Self->RenameBoxId = WIDGET_ID_INVALID;
    if ((event->Index != ANSWER_RENAME) || !Self->HasLayouts)
    {
        return Error_CreateSuccess();
    }
    const unsigned char* Name = NULL;
    Error Result = UITextBox_GetText(screen, BoxId, &Name, NULL);
    size_t Index = UILayouts_GetActiveIndex(&Self->Layouts);
    if ((Result.Code == ErrorCode_Success) && (Name != NULL))
    {
        Result = UIWorkspace_RenameLayout(screen, Self->WorkspaceId, Index, Name);
    }
    char Text[REPORT_CAPACITY];
    if (Result.Code == ErrorCode_Success)
    {
        snprintf(Text, sizeof(Text), "the layout is now called %s", (const char*)Name);
    }
    else
    {
        snprintf(Text, sizeof(Text), "the layout keeps its name: a name is 1 to %zu bytes of printable text, no other layout's",
            UI_LAYOUT_MAX_NAME_BYTES);
    }
    Error_Deconstruct(&Result); // Told, never handed on: a handler's error would stop the screen's update.
    Report(Self, Text);
    return Error_CreateSuccess();
}

/* Asks for the active layout's new name: a dialog with a box holding its name now. */
static Error AskRename(GalleryWorkspace* self)
{
    if (!self->HasLayouts || (UILayouts_GetCount(&self->Layouts) == 0U) || UIScreen_IsPopupOpen(self->Screen, self->RenameDialogId))
    {
        return Error_CreateSuccess();
    }
    const UILayout* Active = UILayouts_Get(&self->Layouts, UILayouts_GetActiveIndex(&self->Layouts));
    static const UIDialogButton Buttons[] =
    {
        { .Id = ANSWER_RENAME, .Text = u8"Rename", .IsDefault = true, .IsCancel = false },
        { .Id = ANSWER_CANCEL, .Text = u8"Cancel", .IsDefault = false, .IsCancel = true },
    };
    UIDialogOptions Options = UIDialogOptions_Create(u8"Rename the layout");
    Options.Message = u8"Its name in the switcher and the Layout menu.";
    Options.Buttons = Buttons;
    Options.ButtonCount = sizeof(Buttons) / sizeof(Buttons[0]);
    Options.OwnerId = self->WorkspaceId;
    uint64_t DialogId = WIDGET_ID_INVALID;
    Error Result = UIKit_CreateDialog(self->Kit, self->Screen, &Options, &DialogId);
    uint64_t ContentId = WIDGET_ID_INVALID;
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIDialog_GetContentId(self->Screen, DialogId, &ContentId);
    }
    uint64_t BoxId = WIDGET_ID_INVALID;
    if (Result.Code == ErrorCode_Success)
    {
        UITextBoxOptions Text = UITextBoxOptions_Create();
        Text.Text = (Active != NULL) ? Active->Name : u8"";
        Text.Limits.MaxBytes = UI_LAYOUT_MAX_NAME_BYTES;
        Result = UIKit_CreateTextBox(self->Kit, self->Screen, ContentId, &Text, &BoxId);
    }
    UserData Data = UserData_FromPointer(self);
    uint64_t Subscription = UI_SCREEN_ID_INVALID;
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIScreen_Subscribe(self->Screen, DialogId, UI_EVENT_KIND_MASK(UIEventKind_Widget), OnRenameAnswer, &Data, &Subscription);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIDialog_Open(self->Screen, DialogId, BoxId);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    self->RenameDialogId = DialogId;
    self->RenameBoxId = BoxId;
    return Error_CreateSuccess();
}

/* What a Layout menu item does; a refusal is told, never handed on. */
static Error OnLayoutItem(GalleryWorkspace* self, uint32_t itemId)
{
    char Text[REPORT_CAPACITY];
    Text[0] = 0;
    Error Result = Error_CreateSuccess();
    size_t Index = self->HasLayouts ? UILayouts_GetActiveIndex(&self->Layouts) : 0U;
    switch (itemId)
    {
        case LAYOUT_MENU_NEW:
        {
            size_t NewIndex = 0U;
            Result = UIWorkspace_NewLayout(self->Screen, self->WorkspaceId, NULL, &NewIndex);
            const UILayout* Made = (Result.Code == ErrorCode_Success) ? UILayouts_Get(&self->Layouts, NewIndex) : NULL;
            snprintf(Text, sizeof(Text), "a new layout, %s, as the arrangement is: change it, and switch back", (Made != NULL) ? (const char*)Made->Name : "");
            break;
        }
        case LAYOUT_MENU_RENAME:
            Result = AskRename(self);
            break;
        case LAYOUT_MENU_DELETE:
            Result = UIWorkspace_DeleteLayout(self->Screen, self->WorkspaceId, Index);
            snprintf(Text, sizeof(Text), "%s", "the layout is deleted: the one before it shows");
            break;
        case LAYOUT_MENU_RESET:
            Result = UIWorkspace_ResetLayout(self->Screen, self->WorkspaceId);
            snprintf(Text, sizeof(Text), "%s", "the layout is the default again");
            break;
        default:
            return Error_CreateSuccess();
    }
    if (Result.Code != ErrorCode_Success)
    {
        snprintf(Text, sizeof(Text), "the Layout menu could not: %s", (Result.Message != NULL) ? (const char*)Result.Message : "no reason given");
    }
    Error_Deconstruct(&Result);
    if (Text[0] != 0)
    {
        Report(self, Text);
    }
    return Error_CreateSuccess();
}

/* The menu bar's handler: the Window menu opens a type through the workspace, the Layout menu acts on the layouts. */
static Error OnMenu(UIScreen* screen, const UIMenuActivation* activation, const UserData* userData)
{
    GalleryWorkspace* Self = UserData_GetPointer(userData);
    uint32_t ItemId = activation->ItemId;
    if ((ItemId < WINDOW_MENU_FIRST_ID) || (ItemId >= (WINDOW_MENU_FIRST_ID + (uint32_t)GALLERY_WORKSPACE_TYPE_COUNT)))
    {
        return OnLayoutItem(Self, ItemId);
    }
    size_t Slot = ItemId - WINDOW_MENU_FIRST_ID;
    uint64_t WindowId = WIDGET_ID_INVALID;
    Error Result = UIWorkspace_OpenType(screen, Self->WorkspaceId, Self->TypeIndices[Slot], &WindowId);
    char Text[REPORT_CAPACITY];
    if (Result.Code != ErrorCode_Success)
    {
        snprintf(Text, sizeof(Text), "the Window menu could not open %s: %s", (const char*)Types[Slot].Name,
            (Result.Message != NULL) ? (const char*)Result.Message : "no reason given");
    }
    else
    {
        snprintf(Text, sizeof(Text), Types[Slot].IsSingle ? "the Window menu showed the %s (there is one)" : "the Window menu opened %s",
            (const char*)Types[Slot].Name);
    }
    Error_Deconstruct(&Result);
    Report(Self, Text);
    return Error_CreateSuccess();
}

/* The menu bar's entries: Window, one item a type, and Layout. */
static Error AddMenus(GalleryWorkspace* self)
{
    uint64_t BarId = WIDGET_ID_INVALID;
    Error Result = UIWorkspace_GetMenuBarId(self->Screen, self->WorkspaceId, &BarId);
    UserData Data = UserData_FromPointer(self);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIMenuBar_SetHandler(self->Screen, BarId, OnMenu, &Data);
    }
    UIMenuItem WindowItems[GALLERY_WORKSPACE_TYPE_COUNT];
    for (size_t Slot = 0U; Slot < GALLERY_WORKSPACE_TYPE_COUNT; Slot++)
    {
        WindowItems[Slot] = (UIMenuItem){ .Id = WINDOW_MENU_FIRST_ID + (uint32_t)Slot, .Label = Types[Slot].Name,
            .Icon = UIElementIcon_FromLetter(Types[Slot].Letter), .IsEnabled = self->IsRegistered };
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIMenuBar_AddEntry(self->Screen, BarId, u8"Window", WindowItems, GALLERY_WORKSPACE_TYPE_COUNT, NULL);
    }
    const UIMenuItem LayoutItems[] =
    {
        { .Id = LAYOUT_MENU_NEW, .Label = u8"New layout", .IsEnabled = self->HasLayouts },
        { .Id = LAYOUT_MENU_RENAME, .Label = u8"Rename layout...", .IsEnabled = self->HasLayouts },
        { .Id = LAYOUT_MENU_DELETE, .Label = u8"Delete layout", .IsEnabled = self->HasLayouts },
        { .IsSeparator = true },
        { .Id = LAYOUT_MENU_RESET, .Label = u8"Reset to the default", .IsEnabled = self->IsRegistered },
    };
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIMenuBar_AddEntry(self->Screen, BarId, u8"Layout", LayoutItems, sizeof(LayoutItems) / sizeof(LayoutItems[0]), NULL);
    }
    return (Result.Code == ErrorCode_Success) ? UITooltip_Set(self->Screen, BarId, u8"Window shows a window of each type (the notes as many "
        u8"times as asked); Layout makes, renames, deletes and resets layouts.") : Result;
}


// Public functions.
Error GalleryWorkspace_Construct(GalleryWorkspace* self, UIWindowTypes* types, KeyRegistry* keys, UIKit* kit, UIScreen* screen, Logger* logger,
    const unsigned char* baseDirectory, GalleryWorkspaceReport report, void* reportContext)
{
    Memory_Zero(self, sizeof(*self));
    self->Types = types;
    self->Kit = kit;
    self->Screen = screen;
    self->Report = report;
    self->ReportContext = reportContext;
    self->WorkspaceId = WIDGET_ID_INVALID;
    self->StatusBarId = WIDGET_ID_INVALID;
    self->SwitcherId = WIDGET_ID_INVALID;
    self->RenameDialogId = WIDGET_ID_INVALID;
    self->RenameBoxId = WIDGET_ID_INVALID;
    self->Picture.ViewId = WIDGET_ID_INVALID;
    self->Picture.TargetViewId = WIDGET_ID_INVALID;
    self->Picture.Speed = PICTURE_START_SPEED;
    if ((types == NULL) || (keys == NULL))
    {
        return Error_CreateSuccess();
    }
    Error Result = KeyRegistry_Intern(keys, u8"tbp:ui_gallery_workspace", &self->SourceKeyId);
    for (size_t Slot = 0U; (Slot < GALLERY_WORKSPACE_TYPE_COUNT) && (Result.Code == ErrorCode_Success); Slot++)
    {
        UIWindowTypeDefinition Definition = UIWindowTypeDefinition_Create(Types[Slot].Name, Types[Slot].Fill);
        Definition.Icon = UIElementIcon_FromLetter(Types[Slot].Letter);
        Definition.Data = UserData_FromPointer(self);
        Definition.FitAcross = Types[Slot].Across;
        Definition.FitDown = Types[Slot].Down;
        Definition.DefaultSize = Types[Slot].Size;
        Definition.IsSingle = Types[Slot].IsSingle;
        Result = UIWindowTypes_Register(types, Types[Slot].Key, &Definition, self->SourceKeyId, &self->TypeIndices[Slot]);
    }
    self->IsRegistered = Result.Code == ErrorCode_Success;
    if (!self->IsRegistered)
    {
        (void)UIWindowTypes_WithdrawSource(types, self->SourceKeyId);
        return Result;
    }
    if (baseDirectory == NULL)
    {
        return Error_CreateSuccess();
    }
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, PATH_INITIAL_CAPACITY, sizeof(unsigned char));
    Result = Path_Append(baseDirectory, GALLERY_WORKSPACE_FILE_NAME, &Path);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UILayouts_Construct(&self->Layouts, Path._data, types, keys, logger);
        self->HasLayouts = Result.Code == ErrorCode_Success;
    }
    Memory_Free(Path._data);
    // A file that cannot be read (or put aside) leaves no layouts: the default shows. It logged why.
    Error LoadResult = self->HasLayouts ? UILayouts_Load(&self->Layouts, NULL) : Error_CreateSuccess();
    Error_Deconstruct(&LoadResult);
    return Result;
}

Error GalleryWorkspace_Save(GalleryWorkspace* self)
{
    if (!self->HasLayouts || !UIWorkspace_IsWorkspace(self->Screen, self->WorkspaceId))
    {
        return Error_CreateSuccess();
    }
    Error Result = UIWorkspace_SaveLayouts(self->Screen, self->WorkspaceId);
    Error Detached = UIWorkspace_SetLayouts(self->Screen, self->WorkspaceId, NULL, NULL, NULL);
    if (Result.Code == ErrorCode_Success)
    {
        return Detached;
    }
    Error_Deconstruct(&Detached);
    return Result;
}

void GalleryWorkspace_Deconstruct(GalleryWorkspace* self)
{
    if (self->HasLayouts)
    {
        UILayouts_Deconstruct(&self->Layouts);
        self->HasLayouts = false;
    }
    if ((self->Types != NULL) && self->IsRegistered)
    {
        (void)UIWindowTypes_WithdrawSource(self->Types, self->SourceKeyId);
    }
    self->IsRegistered = false;
}

Error GalleryWorkspace_Build(GalleryWorkspace* self, uint64_t* outWorkspaceId)
{
    UIWorkspaceOptions Options = UIWorkspaceOptions_Create();
    Options.HasMenuBar = true;
    Options.HasStatusBar = true;
    Options.Types = self->Types;
    Error Result = UIKit_CreateWorkspace(self->Kit, self->Screen, &Options, &self->WorkspaceId);
    *outWorkspaceId = self->WorkspaceId;
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWorkspace_SetShown(self->Screen, self->WorkspaceId, false); // The first page shows first.
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = AddMenus(self);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWorkspace_GetStatusBarId(self->Screen, self->WorkspaceId, &self->StatusBarId);
    }
    if ((Result.Code != ErrorCode_Success) || !self->IsRegistered)
    {
        return (Result.Code == ErrorCode_Success) ? GalleryWorkspace_SetStatus(self, "The program has no window types: there are no windows to show.")
            : Result;
    }
    UserData Data = UserData_FromPointer(self);
    Result = self->HasLayouts ? UIWorkspace_SetLayouts(self->Screen, self->WorkspaceId, &self->Layouts, BuildDefault, &Data)
        : BuildDefault(self->Screen, self->WorkspaceId, &Data);
    uint64_t RightId = WIDGET_ID_INVALID;
    if ((Result.Code == ErrorCode_Success) && self->HasLayouts)
    {
        Result = UIStatusBar_GetSection(self->Screen, self->StatusBarId, UIStatusBarSection_Right, &RightId);
    }
    if ((Result.Code == ErrorCode_Success) && self->HasLayouts)
    {
        Result = UIWorkspace_CreateLayoutSwitcher(self->Kit, self->Screen, RightId, self->WorkspaceId, &self->SwitcherId);
    }
    if ((Result.Code == ErrorCode_Success) && self->HasLayouts)
    {
        Result = UITooltip_Set(self->Screen, self->SwitcherId, u8"The layout shown: choose another to switch to it.");
    }
    return Result;
}

Error GalleryWorkspace_SetShown(GalleryWorkspace* self, bool isShown)
{
    if (!UIWorkspace_IsWorkspace(self->Screen, self->WorkspaceId))
    {
        return Error_CreateSuccess();
    }
    Error Result = UIWorkspace_SetShown(self->Screen, self->WorkspaceId, isShown);
    if ((Result.Code == ErrorCode_Success) && !isShown)
    {
        Result = UIScreen_SetWorkspaceInsets(self->Screen, (UIInsets){ .Left = 0.0f, .Top = 0.0f, .Right = 0.0f, .Bottom = 0.0f });
    }
    return Result;
}

Error GalleryWorkspace_SetStatus(GalleryWorkspace* self, const char* text)
{
    if ((self->StatusBarId == WIDGET_ID_INVALID) || !UIScreen_IsWidgetAlive(self->Screen, self->StatusBarId))
    {
        return Error_CreateSuccess();
    }
    return UIStatusBar_SetMessage(self->Screen, self->StatusBarId, (const unsigned char*)text, 0.0);
}


// Public functions: the picture.
Error GalleryWorkspace_UpdatePicture(GalleryWorkspace* self, GameInput* input, double passedSeconds)
{
    GalleryPicture* Picture = &self->Picture;
    if ((Picture->ViewId == WIDGET_ID_INVALID) || !UIScreen_IsWidgetAlive(self->Screen, Picture->ViewId))
    {
        Picture->ViewId = WIDGET_ID_INVALID;
        return Error_CreateSuccess();
    }
    UIRenderViewInput View;
    Error Result = UIRenderView_TakeInput(self->Screen, Picture->ViewId, &View);
    if ((Result.Code != ErrorCode_Success) || !View.IsShown)
    {
        return Result;
    }
    Picture->Seconds += passedSeconds;
    char Text[REPORT_CAPACITY];
    Text[0] = 0;
    if (View.Wheel.y != 0.0f)
    {
        Picture->Speed = Math_ClampFloat(Picture->Speed * powf(PICTURE_WHEEL_FACTOR, View.Wheel.y), PICTURE_MIN_SPEED, PICTURE_MAX_SPEED);
        snprintf(Text, sizeof(Text), "the picture: the wheel, %s; the stripes run at %.0f pixels a second", View.IsFocused ? "focused" : "not focused",
            (double)Picture->Speed);
    }
    const UIRenderViewButton* Left = &View.Buttons[(int32_t)GameKey_MouseLeft - (int32_t)GameKey_MouseLeft];
    const UIRenderViewButton* Right = &View.Buttons[(int32_t)GameKey_MouseRight - (int32_t)GameKey_MouseLeft];
    if (Left->IsDown && View.HasPointer)
    {
        Picture->Marker = View.Pointer;
        Picture->HasMarker = true;
    }
    if (Right->IsDown)
    {
        Picture->Pan.x += Right->DeltaPixels.x;
        Picture->Pan.y += Right->DeltaPixels.y;
    }
    if (Left->WasPressed)
    {
        snprintf(Text, sizeof(Text), "the picture: a left press at %.2f, %.2f of %d x %d pixels (click %u): the marker follows it", (double)Left->PressPosition.x,
            (double)Left->PressPosition.y, (int)View.PixelSize.X, (int)View.PixelSize.Y, (unsigned)Left->ClickCount);
    }
    if (Right->WasPressed)
    {
        snprintf(Text, sizeof(Text), "%s", "the picture: a right press, the cursor hidden: drag to pan, let go and it comes back where it was");
    }
    if (Left->WasLost || Right->WasLost)
    {
        snprintf(Text, sizeof(Text), "%s", "the picture: a press was lost");
    }
    // Its host reads the keys it keeps while it is entered: Tab and Enter are told, Escape lets go of it (and quits nothing).
    if (View.IsEntered && (input != NULL))
    {
        if (GameInput_IsActionPressed(input, GameAction_UIBack))
        {
            GameInput_ConsumeAction(input, GameAction_UIBack);
            Result = UIScreen_LeaveWidget(self->Screen, UILeaveReason_Keep);
            snprintf(Text, sizeof(Text), "%s", "the picture: Escape reached its host, which let go of the keys");
        }
        else if (GameInput_IsActionPressed(input, GameAction_UINext) || GameInput_IsActionPressed(input, GameAction_UIAccept))
        {
            snprintf(Text, sizeof(Text), "%s", "the picture: a key it keeps (Tab or Enter) reached its host: navigation left it alone");
        }
    }
    if ((Result.Code == ErrorCode_Success) && (Text[0] != 0))
    {
        Report(self, Text);
    }
    return Result;
}

void GalleryWorkspace_PreparePicture(GalleryWorkspace* self)
{
    GalleryPicture* Picture = &self->Picture;
    Picture->IsShown = false;
    if ((Picture->ViewId == WIDGET_ID_INVALID) || !UIScreen_IsWidgetAlive(self->Screen, Picture->ViewId))
    {
        GalleryWorkspace_ReleasePicture(self);
        Picture->HasFailedSize = false;
        return;
    }
    bool IsShown = false;
    Int32Vector Size = { .X = 0, .Y = 0 };
    Error Result = UIRenderView_IsShown(self->Screen, Picture->ViewId, &IsShown);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIRenderView_GetPixelSize(self->Screen, Picture->ViewId, &Size);
    }
    if ((Result.Code != ErrorCode_Success) || !IsShown)
    {
        Error_Deconstruct(&Result);
        return;
    }
    if (!Picture->HasTarget || (Picture->Target.texture.width != Size.X) || (Picture->Target.texture.height != Size.Y))
    {
        if (Picture->HasFailedSize && (Picture->FailedSize.X == Size.X) && (Picture->FailedSize.Y == Size.Y))
        {
            return; // Not tried again until the view's size changes.
        }
        if ((Picture->TargetViewId != WIDGET_ID_INVALID) && UIScreen_IsWidgetAlive(self->Screen, Picture->TargetViewId))
        {
            Result = UIRenderView_ClearTexture(self->Screen, Picture->TargetViewId); // The view lets go of the picture before it is unloaded.
            Error_Deconstruct(&Result);
        }
        GalleryWorkspace_ReleasePicture(self);
        Picture->Target = LoadRenderTexture(Size.X, Size.Y);
        bool IsComplete = (Picture->Target.id != 0U) && (Picture->Target.texture.id != 0U) && rlFramebufferComplete(Picture->Target.id);
        if (!IsComplete)
        {
            if (Picture->Target.id != 0U)
            {
                UnloadRenderTexture(Picture->Target);
            }
            Picture->HasFailedSize = true;
            Picture->FailedSize = Size;
            return;
        }
        Picture->HasTarget = true;
        Picture->HasFailedSize = false;
    }
    if (Picture->TargetViewId != Picture->ViewId)
    {
        Result = UIRenderView_SetTexture(self->Screen, Picture->ViewId, Picture->Target.texture, true);
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            return;
        }
        Picture->TargetViewId = Picture->ViewId;
    }
    Picture->IsShown = true;
}

/* Stripes running across, a grid the right button pans, a dot going round, the marker, and the picture's size: each frame drawn anew at the
 * view's size, so a resize shows at once whether the picture follows. */
void GalleryWorkspace_RenderPicture(const GalleryWorkspace* self)
{
    const GalleryPicture* Picture = &self->Picture;
    if (!Picture->IsShown || !Picture->HasTarget)
    {
        return;
    }
    int Width = Picture->Target.texture.width;
    int Height = Picture->Target.texture.height;
    BeginTextureMode(Picture->Target);
    ClearBackground((Color){ 30, 34, 44, 255 });
    int Offset = (int)fmod(Picture->Seconds * (double)Picture->Speed, (double)(2 * STRIPE_WIDTH));
    for (int X = -2 * STRIPE_WIDTH; X < (Width + Height + (2 * STRIPE_WIDTH)); X += 2 * STRIPE_WIDTH)
    {
        // A stripe leaning right: a quad from the top edge to the bottom edge.
        Vector2 TopLeft = { .x = (float)(X + Offset), .y = 0.0f };
        Vector2 TopRight = { .x = (float)(X + Offset + STRIPE_WIDTH), .y = 0.0f };
        Vector2 BottomLeft = { .x = (float)(X + Offset - Height), .y = (float)Height };
        Vector2 BottomRight = { .x = (float)(X + Offset + STRIPE_WIDTH - Height), .y = (float)Height };
        DrawTriangle(TopLeft, BottomLeft, TopRight, (Color){ 44, 52, 70, 255 });
        DrawTriangle(TopRight, BottomLeft, BottomRight, (Color){ 44, 52, 70, 255 });
    }
    int PanX = ((int)Picture->Pan.x % GRID_SPACING + GRID_SPACING) % GRID_SPACING;
    int PanY = ((int)Picture->Pan.y % GRID_SPACING + GRID_SPACING) % GRID_SPACING;
    for (int X = PanX; X < Width; X += GRID_SPACING)
    {
        DrawRectangle(X, 0, 1, Height, (Color){ 90, 110, 150, 255 });
    }
    for (int Y = PanY; Y < Height; Y += GRID_SPACING)
    {
        DrawRectangle(0, Y, Width, 1, (Color){ 90, 110, 150, 255 });
    }
    double Turn = (2.0 * 3.14159265358979 * Picture->Seconds) / ORBIT_PERIOD_SECONDS;
    float Radius = 0.35f * (float)((Width < Height) ? Width : Height);
    DrawCircle((int)((float)Width * 0.5f + (Radius * (float)cos(Turn))), (int)((float)Height * 0.5f + (Radius * (float)sin(Turn))), DOT_RADIUS,
        (Color){ 230, 180, 90, 255 });
    if (Picture->HasMarker)
    {
        int MarkerX = (int)(Picture->Marker.x * (float)Width);
        int MarkerY = (int)(Picture->Marker.y * (float)Height);
        DrawRectangle(MarkerX - 12, MarkerY - 1, 25, 3, (Color){ 240, 90, 90, 255 });
        DrawRectangle(MarkerX - 1, MarkerY - 12, 3, 25, (Color){ 240, 90, 90, 255 });
    }
    DrawRectangleLines(0, 0, Width, Height, (Color){ 140, 160, 200, 255 });
    DrawText(TextFormat("%d x %d", Width, Height), 8, 8, 20, (Color){ 220, 225, 235, 255 });
    EndTextureMode();
}

void GalleryWorkspace_ReleasePicture(GalleryWorkspace* self)
{
    GalleryPicture* Picture = &self->Picture;
    if (Picture->HasTarget)
    {
        UnloadRenderTexture(Picture->Target);
        Picture->HasTarget = false;
    }
    Picture->TargetViewId = WIDGET_ID_INVALID;
}
