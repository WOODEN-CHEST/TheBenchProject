#include <stdio.h>
#include <string.h>
#include "UIGalleryWorkspace.h"
#include "NamespacedKey.h"
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
#include "wr/WRMemory.h"
#include "wr/WRPath.h"
#include "wr/WRUserData.h"


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

/* The view: where a world will show; meanwhile what to try. */
static Error FillView(UIScreen* screen, uint64_t windowId, uint64_t contentId, const UserData* userData)
{
    (void)windowId;
    (void)userData;
    UIKit* Kit = UIKit_FromScreen(screen);
    Error Result = AddLabel(Kit, screen, contentId, u8"UI gallery: the workspace", u8"heading", false);
    if (Result.Code == ErrorCode_Success)
    {
        Result = AddLabel(Kit, screen, contentId, u8"N the next page. Drag a tab out of its strip: its window floats under the pointer. Let "
            u8"it go on a guide to dock it there (the middle one adds a tab, the others split the area), on a tab strip to add a tab, anywhere "
            u8"else to leave it floating; Escape mid-drag puts it back. A floating window's title bar drags it over the dock the same way. Drag "
            u8"a tab along its strip to reorder it; double click it to maximize its window; right click it to undock, maximize or close it; "
            u8"the wheel over a strip steps through its tabs. Drag the splitters between areas, and shrink the game's window.", u8"hint", true);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = AddLabel(Kit, screen, contentId, u8"The Layout menu makes, renames, deletes and resets layouts; the switcher at the bottom "
            u8"right changes between them. They are kept in ui_gallery_layouts.json next to the game, and come back at the next start; a "
            u8"broken file is put aside as ui_gallery_layouts.json.bad, and the default layout shows.", u8"hint", true);
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
        .Down = UIWindowFit_Scroll, .Size = { 480.0f, 320.0f }, .IsSingle = true, .Fill = FillView },
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
