#include <stdio.h>
#include "EditorInternal.h"
#include "Config.h"
#include "GameFrameManager.h"
#include "Logger.h"
#include "NamespacedKey.h"
#include "Services.h"
#include "UIKit.h"
#include "UIMenuBar.h"
#include "UIRenderView.h"
#include "UIStatusBar.h"
#include "UITextBox.h"
#include "UITooltip.h"
#include "UIWindow.h"
#include "UIWindowTypes.h"
#include "UIWorkspace.h"
#include "wr/WRMemory.h"
#include "wr/WRPath.h"
#include "wr/WRString.h"
#include "wr/WRUserData.h"


// Macros.
/** The theme the editor's UI is drawn with. */
#define EDITOR_THEME_NAME ((const unsigned char*)u8"tbp:test_flat")
/** The source the editor's window types are registered under while it runs. */
#define EDITOR_SOURCE_KEY ((const unsigned char*)u8"tbp:editor")
/** The menus' items: File's, the Window menu's first (an item's id less it is the type's slot) and the Layout menu's. */
#define MENU_QUIT 1001U
#define WINDOW_MENU_FIRST_ID 2000U
#define LAYOUT_MENU_NEW 3001U
#define LAYOUT_MENU_DELETE 3002U
#define LAYOUT_MENU_RESET 3003U
/** How long a message stays in the status line, in seconds. */
#define MESSAGE_SECONDS 6.0
/** The default layout's shares: the controls of the whole width, the notes of the world's column. */
#define CONTROLS_SHARE 0.22f
#define NOTES_SHARE 0.24f
/** The layouts file's path's first capacity. */
#define PATH_INITIAL_CAPACITY ((size_t)256)
/** Room for a message. */
#define MESSAGE_CAPACITY ((size_t)192)


// Types.
/* One of the editor's window types. */
typedef struct EditorWindowTypeStruct
{
    const unsigned char* Key;
    const unsigned char* Name;
    uint32_t Letter;
    UIWindowFit Across;
    UIWindowFit Down;
    Vector2 Size;
    bool IsSingle;
    const unsigned char* Variant;
    UIWindowTypeFill Fill;
} EditorWindowType;


// Static functions: helpers.
static void LogWarning(const EditorFrame* frame, const char* what, Error* error)
{
    if (frame->Services->Logger != NULL)
    {
        Error LogResult = Logger_LogWarningFormatted(frame->Services->Logger, (const unsigned char*)u8"Editor: %s: %s", what,
            (error->Message != NULL) ? (const char*)error->Message : "no reason given");
        Error_Deconstruct(&LogResult);
    }
    Error_Deconstruct(error);
}

static Error SetMessage(EditorShell* self, const char* text)
{
    if ((self->StatusBarId == WIDGET_ID_INVALID) || !UIScreen_IsWidgetAlive(&self->Screen, self->StatusBarId))
    {
        return Error_CreateSuccess();
    }
    return UIStatusBar_SetMessage(&self->Screen, self->StatusBarId, (const unsigned char*)text, MESSAGE_SECONDS);
}


// Static functions: what fills each window.
/* The world: a render view filling the window, the frame its host. A press of the right button on it hides the cursor and looks around; a
 * press on the window's tab or title bar gives it the focus back, entered, so the keys are the world's. */
static Error FillWorld(UIScreen* screen, uint64_t windowId, uint64_t contentId, const UserData* userData)
{
    EditorFrame* Frame = UserData_GetPointer(userData);
    UIRenderViewOptions Options = UIRenderViewOptions_Create();
    Options.Element.Hint = UIStackHint_Fill(1.0f);
    Options.CapturedButtons = UI_BUTTON_BIT(GameKey_MouseRight);
    uint64_t ViewId = WIDGET_ID_INVALID;
    Error Result = UIKit_CreateRenderView(UIKit_FromScreen(screen), screen, contentId, &Options, &ViewId);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWindow_SetDefaultFocus(screen, windowId, ViewId, true);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Frame->Shell.ViewId = ViewId;
    }
    return Result;
}

/* The controls: what the keys and the mouse do, to read. */
static Error FillControls(UIScreen* screen, uint64_t windowId, uint64_t contentId, const UserData* userData)
{
    (void)windowId;
    (void)userData;
    UIKit* Kit = UIKit_FromScreen(screen);
    UILabelOptions Heading = UILabelOptions_Create(u8"Flying around");
    Heading.Element.Variant = u8"heading";
    uint64_t Id = WIDGET_ID_INVALID;
    Error Result = UIKit_CreateLabel(Kit, screen, contentId, &Heading, &Id);
    static const unsigned char* const Lines[] =
    {
        u8"Hold the right mouse button in the world to look around: the cursor hides, and comes back where it was when you let go.",
        u8"While it is held, W, A, S and D fly, E rises and Q sinks; Shift flies three times as fast. The wheel over the world changes the speed.",
        u8"The keys reach the world only while it has the focus: a click in it, or on its tab or title bar, gives it the focus; a click anywhere "
        u8"else takes it away.",
        u8"Drag a tab along its strip, onto another area's guides or out of the dock. The Window menu opens each window, the Layout menu keeps "
        u8"arrangements, and the editor remembers yours."
    };
    for (size_t Index = 0U; (Index < (sizeof(Lines) / sizeof(Lines[0]))) && (Result.Code == ErrorCode_Success); Index++)
    {
        UILabelOptions Line = UILabelOptions_Create(Lines[Index]);
        Line.Element.Variant = u8"hint";
        Line.IsWrapped = true;
        Result = UIKit_CreateLabel(Kit, screen, contentId, &Line, &Id);
    }
    return Result;
}

/* Notes: a text box filling the window, where typing never moves the camera. */
static Error FillNotes(UIScreen* screen, uint64_t windowId, uint64_t contentId, const UserData* userData)
{
    (void)userData;
    UITextBoxOptions Text = UITextBoxOptions_CreateMultiline();
    Text.Placeholder = u8"Notes: type here. Keys typed here never reach the world.";
    Text.Element.Hint = UIStackHint_Fill(1.0f);
    uint64_t Id = WIDGET_ID_INVALID;
    Error Result = UIKit_CreateTextBox(UIKit_FromScreen(screen), screen, contentId, &Text, &Id);
    return (Result.Code == ErrorCode_Success) ? UIWindow_SetDefaultFocus(screen, windowId, Id, true) : Result;
}


// Fields.
static const EditorWindowType WindowTypes[EditorWindowSlot_Count] =
{
    [EditorWindowSlot_World] = { .Key = u8"tbp:editor_world", .Name = u8"World", .Letter = (uint32_t)'W', .Across = UIWindowFit_Fit,
        .Down = UIWindowFit_Fit, .Size = { 640.0f, 400.0f }, .IsSingle = true, .Variant = u8"view", .Fill = FillWorld },
    [EditorWindowSlot_Controls] = { .Key = u8"tbp:editor_controls", .Name = u8"Controls", .Letter = (uint32_t)'C', .Across = UIWindowFit_Fit,
        .Down = UIWindowFit_Scroll, .Size = { 280.0f, 300.0f }, .IsSingle = true, .Variant = NULL, .Fill = FillControls },
    [EditorWindowSlot_Notes] = { .Key = u8"tbp:editor_notes", .Name = u8"Notes", .Letter = (uint32_t)'N', .Across = UIWindowFit_Scroll,
        .Down = UIWindowFit_Scroll, .Size = { 360.0f, 200.0f }, .IsSingle = false, .Variant = NULL, .Fill = FillNotes },
};


// Static functions: the default layout.
/* The world in the middle, alone in its area; the controls at its left; notes under it. Nothing keeps the focus: a press chooses. */
static Error BuildDefault(UIScreen* screen, uint64_t workspaceId, const UserData* userData)
{
    EditorFrame* Frame = UserData_GetPointer(userData);
    uint64_t Windows[EditorWindowSlot_Count];
    Error Result = Error_CreateSuccess();
    for (size_t Slot = 0U; (Slot < (size_t)EditorWindowSlot_Count) && (Result.Code == ErrorCode_Success); Slot++)
    {
        Windows[Slot] = WIDGET_ID_INVALID;
        Result = UIWorkspace_OpenType(screen, workspaceId, Frame->Shell.TypeIndices[Slot], &Windows[Slot]);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWorkspace_Dock(screen, workspaceId, Windows[EditorWindowSlot_World], WIDGET_ID_INVALID, UIDockZone_Center);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWorkspace_Dock(screen, workspaceId, Windows[EditorWindowSlot_Controls], Windows[EditorWindowSlot_World], UIDockZone_Left);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWorkspace_Dock(screen, workspaceId, Windows[EditorWindowSlot_Notes], Windows[EditorWindowSlot_World], UIDockZone_Bottom);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWorkspace_SetAreaShare(screen, workspaceId, Windows[EditorWindowSlot_Controls], CONTROLS_SHARE);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWorkspace_SetAreaShare(screen, workspaceId, Windows[EditorWindowSlot_Notes], NOTES_SHARE);
    }
    return (Result.Code == ErrorCode_Success) ? UIScreen_SetFocusedWidget(screen, NULL) : Result;
}


// Static functions: the menus.
static Error OnLayoutItem(EditorFrame* frame, uint32_t itemId)
{
    EditorShell* Shell = &frame->Shell;
    char Text[MESSAGE_CAPACITY];
    Text[0] = 0;
    Error Result = Error_CreateSuccess();
    switch (itemId)
    {
        case LAYOUT_MENU_NEW:
        {
            size_t NewIndex = 0U;
            Result = UIWorkspace_NewLayout(&Shell->Screen, Shell->WorkspaceId, NULL, &NewIndex);
            const UILayout* Made = (Result.Code == ErrorCode_Success) ? UILayouts_Get(&Shell->Layouts, NewIndex) : NULL;
            snprintf(Text, sizeof(Text), "A new layout, %s, as the windows stand: the switcher changes between layouts.",
                (Made != NULL) ? (const char*)Made->Name : "");
            break;
        }
        case LAYOUT_MENU_DELETE:
            Result = UIWorkspace_DeleteLayout(&Shell->Screen, Shell->WorkspaceId, UILayouts_GetActiveIndex(&Shell->Layouts));
            snprintf(Text, sizeof(Text), "%s", "The layout is deleted: the one before it shows.");
            break;
        case LAYOUT_MENU_RESET:
            Result = UIWorkspace_ResetLayout(&Shell->Screen, Shell->WorkspaceId);
            snprintf(Text, sizeof(Text), "%s", "The layout is the default again.");
            break;
        default:
            return Error_CreateSuccess();
    }
    if (Result.Code != ErrorCode_Success)
    {
        snprintf(Text, sizeof(Text), "The Layout menu could not: %s", (Result.Message != NULL) ? (const char*)Result.Message : "no reason given");
    }
    Error_Deconstruct(&Result); // Told, never handed on: a handler's error would stop the screen's update.
    return SetMessage(Shell, Text);
}

/* The menu bar's handler: File quits, the Window menu opens a type through the workspace (the world's comes back where it was), the Layout
 * menu acts on the layouts. */
static Error OnMenu(UIScreen* screen, const UIMenuActivation* activation, const UserData* userData)
{
    EditorFrame* Frame = UserData_GetPointer(userData);
    uint32_t ItemId = activation->ItemId;
    if (ItemId == MENU_QUIT)
    {
        if (Frame->Services->FrameManager != NULL)
        {
            GameFrameManager_RequestStop(Frame->Services->FrameManager);
        }
        return Error_CreateSuccess();
    }
    if ((ItemId < WINDOW_MENU_FIRST_ID) || (ItemId >= (WINDOW_MENU_FIRST_ID + (uint32_t)EditorWindowSlot_Count)))
    {
        return OnLayoutItem(Frame, ItemId);
    }
    size_t Slot = ItemId - WINDOW_MENU_FIRST_ID;
    uint64_t WindowId = WIDGET_ID_INVALID;
    Error Result = UIWorkspace_OpenType(screen, Frame->Shell.WorkspaceId, Frame->Shell.TypeIndices[Slot], &WindowId);
    if (Result.Code != ErrorCode_Success)
    {
        char Text[MESSAGE_CAPACITY];
        snprintf(Text, sizeof(Text), "The Window menu could not open %s: %s", (const char*)WindowTypes[Slot].Name,
            (Result.Message != NULL) ? (const char*)Result.Message : "no reason given");
        Error_Deconstruct(&Result);
        return SetMessage(&Frame->Shell, Text);
    }
    return Error_CreateSuccess();
}

static Error AddMenus(EditorFrame* frame)
{
    EditorShell* Shell = &frame->Shell;
    uint64_t BarId = WIDGET_ID_INVALID;
    Error Result = UIWorkspace_GetMenuBarId(&Shell->Screen, Shell->WorkspaceId, &BarId);
    UserData Data = UserData_FromPointer(frame);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIMenuBar_SetHandler(&Shell->Screen, BarId, OnMenu, &Data);
    }
    const UIMenuItem FileItems[] = { { .Id = MENU_QUIT, .Label = u8"Quit", .IsEnabled = true } };
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIMenuBar_AddEntry(&Shell->Screen, BarId, u8"File", FileItems, sizeof(FileItems) / sizeof(FileItems[0]), NULL);
    }
    UIMenuItem WindowItems[EditorWindowSlot_Count];
    for (size_t Slot = 0U; Slot < (size_t)EditorWindowSlot_Count; Slot++)
    {
        WindowItems[Slot] = (UIMenuItem){ .Id = WINDOW_MENU_FIRST_ID + (uint32_t)Slot, .Label = WindowTypes[Slot].Name,
            .Icon = UIElementIcon_FromLetter(WindowTypes[Slot].Letter), .IsEnabled = true };
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIMenuBar_AddEntry(&Shell->Screen, BarId, u8"Window", WindowItems, (size_t)EditorWindowSlot_Count, NULL);
    }
    const UIMenuItem LayoutItems[] =
    {
        { .Id = LAYOUT_MENU_NEW, .Label = u8"New layout", .IsEnabled = Shell->HasLayouts },
        { .Id = LAYOUT_MENU_DELETE, .Label = u8"Delete layout", .IsEnabled = Shell->HasLayouts },
        { .IsSeparator = true },
        { .Id = LAYOUT_MENU_RESET, .Label = u8"Reset to the default", .IsEnabled = Shell->HasLayouts },
    };
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIMenuBar_AddEntry(&Shell->Screen, BarId, u8"Layout", LayoutItems, sizeof(LayoutItems) / sizeof(LayoutItems[0]), NULL);
    }
    return Result;
}


// Static functions: building.
static Error RegisterTypes(EditorFrame* frame)
{
    EditorShell* Shell = &frame->Shell;
    Services* Shared = frame->Services;
    Shell->Types = Shared->WindowTypes;
    Error Result = KeyRegistry_Intern(Shared->Keys, EDITOR_SOURCE_KEY, &Shell->SourceKeyId);
    for (size_t Slot = 0U; (Slot < (size_t)EditorWindowSlot_Count) && (Result.Code == ErrorCode_Success); Slot++)
    {
        const EditorWindowType* Type = &WindowTypes[Slot];
        UIWindowTypeDefinition Definition = UIWindowTypeDefinition_Create(Type->Name, Type->Fill);
        Definition.Icon = UIElementIcon_FromLetter(Type->Letter);
        Definition.Data = UserData_FromPointer(frame);
        Definition.FitAcross = Type->Across;
        Definition.FitDown = Type->Down;
        Definition.DefaultSize = Type->Size;
        Definition.IsSingle = Type->IsSingle;
        Definition.Variant = Type->Variant;
        Result = UIWindowTypes_Register(Shell->Types, Type->Key, &Definition, Shell->SourceKeyId, &Shell->TypeIndices[Slot]);
    }
    Shell->AreTypesRegistered = Result.Code == ErrorCode_Success;
    if (!Shell->AreTypesRegistered)
    {
        (void)UIWindowTypes_WithdrawSource(Shell->Types, Shell->SourceKeyId);
    }
    return Result;
}

/* The layouts kept in the game's folder; none without a folder, or when the file's path cannot be made (the default shows then). A file that
 * cannot be read or is put aside leaves the default too: it logged why. */
static void ConstructLayouts(EditorFrame* frame)
{
    EditorShell* Shell = &frame->Shell;
    Services* Shared = frame->Services;
    if (Shared->BaseDirectory == NULL)
    {
        return;
    }
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, PATH_INITIAL_CAPACITY, sizeof(unsigned char));
    Error Result = Path_Append(Shared->BaseDirectory, EDITOR_LAYOUTS_FILE_NAME, &Path);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UILayouts_Construct(&Shell->Layouts, Path._data, Shared->WindowTypes, Shared->Keys, Shared->Logger);
        Shell->HasLayouts = Result.Code == ErrorCode_Success;
    }
    Memory_Free(Path._data);
    if (Result.Code != ErrorCode_Success)
    {
        LogWarning(frame, "the layouts are not kept", &Result);
        return;
    }
    Error LoadResult = UILayouts_Load(&Shell->Layouts, NULL);
    Error_Deconstruct(&LoadResult);
}

static Error BuildWorkspace(EditorFrame* frame)
{
    EditorShell* Shell = &frame->Shell;
    UIWorkspaceOptions Options = UIWorkspaceOptions_Create();
    Options.HasMenuBar = true;
    Options.HasStatusBar = true;
    Options.Types = frame->Services->WindowTypes;
    Error Result = UIKit_CreateWorkspace(&Shell->Kit, &Shell->Screen, &Options, &Shell->WorkspaceId);
    if (Result.Code == ErrorCode_Success)
    {
        Result = AddMenus(frame);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWorkspace_GetStatusBarId(&Shell->Screen, Shell->WorkspaceId, &Shell->StatusBarId);
    }
    uint64_t LeftId = WIDGET_ID_INVALID;
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIStatusBar_GetSection(&Shell->Screen, Shell->StatusBarId, UIStatusBarSection_Left, &LeftId);
    }
    if (Result.Code == ErrorCode_Success)
    {
        UILabelOptions Place = UILabelOptions_Create(u8"");
        Result = UIKit_CreateLabel(&Shell->Kit, &Shell->Screen, LeftId, &Place, &Shell->PlaceLabelId);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UITooltip_Set(&Shell->Screen, Shell->PlaceLabelId, u8"Where the camera is, in metres, and how fast it flies.");
    }
    UserData Data = UserData_FromPointer(frame);
    if (Result.Code == ErrorCode_Success)
    {
        Result = Shell->HasLayouts ? UIWorkspace_SetLayouts(&Shell->Screen, Shell->WorkspaceId, &Shell->Layouts, BuildDefault, &Data)
            : BuildDefault(&Shell->Screen, Shell->WorkspaceId, &Data);
    }
    uint64_t RightId = WIDGET_ID_INVALID;
    if ((Result.Code == ErrorCode_Success) && Shell->HasLayouts)
    {
        Result = UIStatusBar_GetSection(&Shell->Screen, Shell->StatusBarId, UIStatusBarSection_Right, &RightId);
    }
    uint64_t SwitcherId = WIDGET_ID_INVALID;
    if ((Result.Code == ErrorCode_Success) && Shell->HasLayouts)
    {
        Result = UIWorkspace_CreateLayoutSwitcher(&Shell->Kit, &Shell->Screen, RightId, Shell->WorkspaceId, &SwitcherId);
    }
    if ((Result.Code == ErrorCode_Success) && Shell->HasLayouts)
    {
        Result = UITooltip_Set(&Shell->Screen, SwitcherId, u8"The layout shown: choose another to switch to it.");
    }
    return (Result.Code == ErrorCode_Success) ? SetMessage(Shell, "Hold the right mouse button in the world to look around and fly; the "
        "Controls window says the rest.") : Result;
}


// Internal functions (declared in EditorInternal.h).
Error EditorShell_Construct(EditorShell* self, EditorFrame* frame)
{
    Services* Shared = frame->Services;
    self->WorkspaceId = WIDGET_ID_INVALID;
    self->StatusBarId = WIDGET_ID_INVALID;
    self->PlaceLabelId = WIDGET_ID_INVALID;
    self->ViewId = WIDGET_ID_INVALID;
    float PlayerScale = (Shared->Config != NULL) ? Shared->Config->UIScale : 1.0f;
    float Aspect = (Shared->FrameManager != NULL) ? GameFrameManager_GetTargetAspectRatio(Shared->FrameManager) : (16.0f / 9.0f);
    Vector2 Position = (Shared->FrameManager != NULL) ? GameFrameManager_GetTargetAreaPosition(Shared->FrameManager)
        : RenderTargetPosition_Centered();
    UIWindowHost_Construct(&self->Host, UIScalePolicy_ConstantPixels(), PlayerScale, Aspect, Position);
    Error Result = TextComponentFactory_Construct(&self->TextFactory);
    self->IsTextFactoryConstructed = Result.Code == ErrorCode_Success;
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWidgetFactory_Construct(&self->WidgetFactory);
        self->IsWidgetFactoryConstructed = Result.Code == ErrorCode_Success;
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIKit_Construct(&self->Kit, &self->WidgetFactory, &self->TextFactory, Shared->Assets, Shared->Logger);
        self->IsKitConstructed = Result.Code == ErrorCode_Success;
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIScreen_Construct(&self->Screen, &self->WidgetFactory, Shared->Logger);
        self->IsScreenConstructed = Result.Code == ErrorCode_Success;
    }
    if ((Result.Code == ErrorCode_Success) && (Shared->Assets != NULL))
    {
        // Without its theme the UI still works, in plain boxes (each missing part is logged once).
        Error ThemeResult = UIScreen_SetTheme(&self->Screen, Shared->Assets, EDITOR_THEME_NAME);
        if (ThemeResult.Code != ErrorCode_Success)
        {
            LogWarning(frame, "its theme did not load", &ThemeResult);
        }
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = RegisterTypes(frame);
    }
    if (Result.Code == ErrorCode_Success)
    {
        ConstructLayouts(frame);
        Result = BuildWorkspace(frame);
    }
    return Result;
}

void EditorShell_Deconstruct(EditorShell* self)
{
    if (self->IsScreenConstructed && self->HasLayouts && UIWorkspace_IsWorkspace(&self->Screen, self->WorkspaceId))
    {
        // While its windows are there to capture: the arrangement as it is now is kept.
        Error Saved = UIWorkspace_SaveLayouts(&self->Screen, self->WorkspaceId);
        Error Detached = UIWorkspace_SetLayouts(&self->Screen, self->WorkspaceId, NULL, NULL, NULL);
        if (Saved.Code != ErrorCode_Success)
        {
            Logger* Log = UIScreen_GetLogger(&self->Screen);
            if (Log != NULL)
            {
                Error LogResult = Logger_LogWarningFormatted(Log, (const unsigned char*)u8"Editor: the layouts were not saved: %s",
                    (Saved.Message != NULL) ? (const char*)Saved.Message : "no reason given");
                Error_Deconstruct(&LogResult);
            }
        }
        Error_Deconstruct(&Saved);
        Error_Deconstruct(&Detached);
    }
    UIWindowHost_Deconstruct(&self->Host);
    if (self->IsScreenConstructed)
    {
        Error Result = UIScreen_Deconstruct(&self->Screen);
        Error_Deconstruct(&Result);
        self->IsScreenConstructed = false;
    }
    if (self->HasLayouts)
    {
        UILayouts_Deconstruct(&self->Layouts);
        self->HasLayouts = false;
    }
    if (self->AreTypesRegistered)
    {
        (void)UIWindowTypes_WithdrawSource(self->Types, self->SourceKeyId); // Once their windows have gone.
        self->AreTypesRegistered = false;
    }
    if (self->IsKitConstructed)
    {
        Error Result = UIKit_Deconstruct(&self->Kit);
        Error_Deconstruct(&Result);
        self->IsKitConstructed = false;
    }
    if (self->IsWidgetFactoryConstructed)
    {
        Error Result = UIWidgetFactory_Deconstruct(&self->WidgetFactory);
        Error_Deconstruct(&Result);
        self->IsWidgetFactoryConstructed = false;
    }
    if (self->IsTextFactoryConstructed)
    {
        Error Result = TextComponentFactory_Deconstruct(&self->TextFactory);
        Error_Deconstruct(&Result);
        self->IsTextFactoryConstructed = false;
    }
}

Error EditorShell_SetPlaceText(EditorShell* self, const char* text)
{
    bool IsSame = false;
    Error Compare = StringUTF8_EqualsExact((const unsigned char*)text, self->PlaceText, &IsSame);
    Error_Deconstruct(&Compare);
    if (IsSame || !UIScreen_IsWidgetAlive(&self->Screen, self->PlaceLabelId))
    {
        return Error_CreateSuccess();
    }
    snprintf((char*)self->PlaceText, sizeof(self->PlaceText), "%s", text);
    return UILabel_SetText(&self->Screen, self->PlaceLabelId, self->PlaceText);
}
