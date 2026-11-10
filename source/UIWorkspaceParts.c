#include "UIContextMenu.h"
#include "UIScreen.h"
#include "UITooltip.h"
#include "UIWidgetFactory.h"
#include "UIWorkspace.h"
#include "UIWorkspaceInternal.h"
#include "wr/WRChar.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"


/*
 * The kit's own widget types a workspace is made of, drawn from the theme's workspace block: a docked window's tab (its icon, its name while
 * selected; a press selects it and gives its window the focus, a drag reorders it or tears its window out, a double click maximizes it), a
 * splitter between two areas (a drag sets its split's ratio), and what shows the drop zones while a window is dragged over the dock (a root
 * of the Overlay layer).
 */


// Types.
/* A docked window's tab. */
typedef struct DockTabStruct
{
    Widget Base;
    UIElementLook Look;
    UIElementText Title;
    UIElementIconView Icon;
    UITextContent TitleContent;
    UIElementIcon ShownIcon;
    uint64_t WorkspaceId;
    uint64_t WindowId;
    /* Whether its window shows in its area, as its last update saw. */
    bool IsSelected;
    /* A left press on it is held, and whether it is a double click's second (its release maximizes). */
    bool IsHeld;
    bool IsToggleOnRelease;
    /* The buttons whose presses it holds, for its look. */
    uint8_t HeldButtons;
} DockTab;

/* A splitter between two areas. */
typedef struct DockSplitterStruct
{
    Widget Base;
    UIElementLook Look;
    uint64_t WorkspaceId;
    int32_t Split;
    bool IsDragging;
} DockSplitter;

/* What shows the drop zones: the guides of the area under a dragged window, the one under the pointer lit, and where the window would go. */
typedef struct DockZonesStruct
{
    Widget Base;
    UIElementLook Look;
    uint64_t WorkspaceId;
    bool IsShowing;
    UIWorkspaceDropView View;
} DockZones;


// Static functions: helpers.
static float RoundToPixel(float value)
{
    return Math_FloorFloat(value + 0.5f);
}

static UserData MakeIdData(uint64_t id)
{
    UserData Data = UserData_CreateEmpty();
    Memory_Copy(&id, Data.Bytes, sizeof(id));
    return Data;
}

static Error CreatePart(UIScreen* screen, uint64_t parentId, uint64_t typeId, void* args, uint64_t* outId)
{
    *outId = WIDGET_ID_INVALID;
    UIKit* Kit = UIKit_FromScreen(screen);
    Widget* Made = NULL;
    Error Result = UIWidgetFactory_CreateWidget(Kit->_factory, screen, typeId, args, &Made);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    uint64_t Id = Widget_GetId(Made);
    Result = Widget_InitializeWidget(Made);
    Widget* Parent = NULL;
    if (Result.Code == ErrorCode_Success)
    {
        bool IsParentGone = (parentId != WIDGET_ID_INVALID) && !UIScreen_TryGetWidgetById(screen, parentId, &Parent);
        Result = (IsParentGone || !UIScreen_TryGetWidgetById(screen, Id, &Made))
            ? Error_Construct2(ErrorCode_InvalidOperation, "UIWorkspace: a part's workspace is gone, or a hook took the part away.") : Error_CreateSuccess();
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = (parentId != WIDGET_ID_INVALID) ? Widget_AddSubWidget(Parent, Made) : UIScreen_AddWidget(screen, Made);
    }
    if (Result.Code != ErrorCode_Success)
    {
        if (UIScreen_TryGetWidgetById(screen, Id, &Made))
        {
            Error Cleanup = Widget_Destroy(Made);
            Error_Deconstruct(&Cleanup);
        }
        return Result;
    }
    *outId = Id;
    return Error_CreateSuccess();
}

/* A window's icon, or the first letter of its title (upper case) while it has none: a tab always shows something. */
static UIElementIcon GetShownIcon(const UIWindowCapability* window)
{
    if (window->_icon.Kind != UIElementIconKind_None)
    {
        return window->_icon;
    }
    const unsigned char* Title = UITextContent_Get(&window->_title);
    CodePoint First = ((Title != NULL) && (Title[0] != 0U) && CharUTF8_IsCharValid(Title)) ? CharUTF8_GetCodePoint(Title) : (CodePoint)'?';
    if ((First >= (CodePoint)'a') && (First <= (CodePoint)'z'))
    {
        First -= (CodePoint)('a' - 'A'); // A placeholder: upper case is enough for the letters without art.
    }
    return UIElementIcon_FromLetter((First > 0) ? (uint32_t)First : (uint32_t)'?');
}

static const UIWindowCapability* FindWindowCapability(UIScreen* screen, uint64_t windowId)
{
    Widget* Window = NULL;
    UIKit* Kit = UIKit_FromScreen(screen);
    if ((Kit == NULL) || !UIScreen_TryGetWidgetById(screen, windowId, &Window))
    {
        return NULL;
    }
    return Widget_GetCapability(Window, UIKit_GetCapabilityId(Kit, UIElementCapability_Window));
}


// Static functions: the tab.
static Error Tab_Construct(void* widget, void* args)
{
    DockTab* Tab = widget;
    const UIDockTabArgs* Arguments = args;
    if (Arguments == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIWorkspace: a tab needs its workspace and its window.");
    }
    Tab->WorkspaceId = Arguments->WorkspaceId;
    Tab->WindowId = Arguments->WindowId;
    Tab->IsSelected = false;
    Tab->IsHeld = false;
    Tab->IsToggleOnRelease = false;
    Tab->HeldButtons = 0U;
    Tab->ShownIcon = UIElementIcon_None();
    Error Result = UIElementLook_Construct(&Tab->Look, UIElementKind_Workspace, NULL);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UITextContent_Construct(&Tab->TitleContent, NULL);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = UIElementText_Construct(&Tab->Title, UIKit_GetTextFactory(Arguments->Kit), UIKit_GetAssets(Arguments->Kit), UIKit_GetLogger(Arguments->Kit));
    if (Result.Code != ErrorCode_Success)
    {
        UITextContent_Deconstruct(&Tab->TitleContent);
        return Result;
    }
    Result = UIElementIconView_Construct(&Tab->Icon, UIKit_GetTextFactory(Arguments->Kit), UIKit_GetAssets(Arguments->Kit), UIKit_GetLogger(Arguments->Kit));
    if (Result.Code != ErrorCode_Success)
    {
        Error Cleanup = UIElementText_Deconstruct(&Tab->Title);
        Error_Deconstruct(&Cleanup);
        UITextContent_Deconstruct(&Tab->TitleContent);
        return Result;
    }
    // A press on a tab acts on nothing in its window: the window gives the focus itself (back to what had it inside).
    return Widget_SetKeepsFocusOnPress(&Tab->Base, true);
}

static Error Tab_OnDeconstruct(void* self)
{
    DockTab* Tab = self;
    Error Result = UIElementIconView_Deconstruct(&Tab->Icon);
    Error TextResult = UIElementText_Deconstruct(&Tab->Title);
    UITextContent_Deconstruct(&Tab->TitleContent);
    if (Result.Code == ErrorCode_Success)
    {
        return TextResult;
    }
    Error_Deconstruct(&TextResult);
    return Result;
}

/* Its window's name and icon, whether it is selected, and the state its look shows. */
static Error Tab_Update(void* self, ProgramTime time)
{
    DockTab* Tab = self;
    UIScreen* Screen = Widget_GetScreen(&Tab->Base);
    const UIWindowCapability* Window = FindWindowCapability(Screen, Tab->WindowId);
    Error Result = Error_CreateSuccess();
    if (Window != NULL)
    {
        bool HasChanged = false;
        Result = UITextContent_Set(&Tab->TitleContent, UITextContent_Get(&Window->_title), &HasChanged);
        UIElementIcon Shown = GetShownIcon(Window);
        if (!UIElementIcon_Equals(&Shown, &Tab->ShownIcon))
        {
            Tab->ShownIcon = Shown;
            HasChanged = true;
        }
        if (HasChanged)
        {
            Widget_InvalidateMeasure(&Tab->Base);
        }
    }
    const UIDockTree* Tree = UIWorkspace_GetTree(Screen, Tab->WorkspaceId);
    int32_t Leaf = UI_DOCK_NODE_NONE;
    bool IsSelected = (Tree != NULL) && UIDockTree_FindTab(Tree, Tab->WindowId, &Leaf, NULL) && (UIDockTree_GetSelected(Tree, Leaf) == Tab->WindowId);
    if (IsSelected != Tab->IsSelected)
    {
        Tab->IsSelected = IsSelected;
        Widget_InvalidateMeasure(&Tab->Base); // The selected one shows its name.
    }
    UIElementState State = UIElementState_Choose(true, Tab->HeldButtons != 0U, false, false, Tab->IsSelected, Widget_IsHovered(&Tab->Base), false);
    UIElementLook_Update(&Tab->Look, Screen, State, time.PassedTime);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIElementText_Refresh(&Tab->Title, &Tab->Base, &Tab->TitleContent);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIElementIconView_Refresh(&Tab->Icon, &Tab->Base, &Tab->ShownIcon);
    }
    return (Result.Code == ErrorCode_Success) ? UIElementIconView_Update(&Tab->Icon, time) : Result;
}

/* Its padding, its icon, and while it is selected its name after it, no wider than the theme's tab_max_width; a strip's height. */
static Error Tab_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)context;
    (void)availableWidth;
    DockTab* Tab = self;
    const UIScreen* Screen = Widget_GetScreen(&Tab->Base);
    Error Result = UIElementText_Refresh(&Tab->Title, &Tab->Base, &Tab->TitleContent);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIElementIconView_Refresh(&Tab->Icon, &Tab->Base, &Tab->ShownIcon);
    }
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Tab->Look, Screen);
    float Scale = Widget_GetEffectiveScale(&Tab->Base);
    float Padding = UIThemeStyle_GetNumber(Style, UIElementKind_Workspace, UIWorkspacePart_TabPadding);
    float Width = (2.0f * Padding) + (UIElementIconView_GetPixelSize(&Tab->Icon, Screen).x / Scale);
    if (Tab->IsSelected)
    {
        float Text = UIElementText_GetPixelSize(&Tab->Title, Screen).x / Scale;
        float Spacing = UIThemeStyle_GetNumber(Style, UIElementKind_Workspace, UIWorkspacePart_IconSpacing);
        float MaxWidth = UIThemeStyle_GetNumber(Style, UIElementKind_Workspace, UIWorkspacePart_TabMaxWidth);
        Width = Math_MaxFloat(Width, Math_MinFloat(Width + Spacing + Text, MaxWidth));
    }
    *outSize = (Vector2){ .x = Width, .y = Math_MaxFloat(UIThemeStyle_GetNumber(Style, UIElementKind_Workspace, UIWorkspacePart_TabHeight),
        UI_WINDOW_MIN_TITLE_HEIGHT) };
    return Result;
}

static Error Tab_Render(void* self, UIRenderContext* context)
{
    DockTab* Tab = self;
    Rectangle Pixels = UIRenderContext_GetPixelBox(context);
    if (UIElement_IsBoxEmpty(Pixels))
    {
        return Error_CreateSuccess();
    }
    const UIScreen* Screen = Widget_GetScreen(&Tab->Base);
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Tab->Look, Screen);
    float Scale = Widget_GetEffectiveScale(&Tab->Base);
    Error Result = UIElementLook_DrawBox(&Tab->Look, Style, UIWorkspacePart_Tab, context, Pixels, Scale);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Color TextColor = UIElementLook_BlendColors(&Tab->Look, UIThemeStyle_GetColors(Style, UIWorkspacePart_TabText), WHITE);
    float Padding = RoundToPixel(UIThemeStyle_GetNumber(Style, UIElementKind_Workspace, UIWorkspacePart_TabPadding) * Scale);
    Vector2 Icon = UIElementIconView_GetPixelSize(&Tab->Icon, Screen);
    Vector2 Text = UIElementText_GetPixelSize(&Tab->Title, Screen);
    RenderContext* Render = UIRenderContext_GetRenderContext(context);
    RenderContext_PushClip(Render, Pixels);
    float X = Pixels.x + Padding;
    Result = UIElementIconView_Draw(&Tab->Icon, context, X, Pixels.y + RoundToPixel((Pixels.height - Icon.y) * 0.5f), TextColor);
    if ((Result.Code == ErrorCode_Success) && Tab->IsSelected)
    {
        X += Icon.x + RoundToPixel(UIThemeStyle_GetNumber(Style, UIElementKind_Workspace, UIWorkspacePart_IconSpacing) * Scale);
        Rectangle Room = { .x = X, .y = Pixels.y, .width = Math_MaxFloat(0.0f, (Pixels.x + Pixels.width - Padding) - X), .height = Pixels.height };
        RenderContext_PushClip(Render, Room);
        Result = UIElementText_Draw(&Tab->Title, context, X, Pixels.y + RoundToPixel((Pixels.height - Text.y) * 0.5f), TextColor);
        RenderContext_PopClip(Render);
    }
    RenderContext_PopClip(Render);
    return Result;
}

/* A press of any button selects it and gives its window the focus; a left one may drag it along its strip or out of it, and a second in a
 * row maximizes its window out of the dock when it is let go. */
static Error Tab_OnMouseInput(void* self, const WidgetMouseInputArgs* args)
{
    DockTab* Tab = self;
    UIScreen* Screen = Widget_GetScreen(&Tab->Base);
    uint64_t WorkspaceId = Tab->WorkspaceId;
    uint64_t WindowId = Tab->WindowId;
    uint8_t Bit = UI_BUTTON_BIT(args->Button);
    switch (args->Type)
    {
        case UIMouseInputType_ButtonPress:
        {
            Tab->HeldButtons = (uint8_t)(Tab->HeldButtons | Bit);
            if (args->Button == GameKey_MouseLeft)
            {
                Tab->IsHeld = true;
                Tab->IsToggleOnRelease = args->ClickCount == 2U;
            }
            Error Result = UIElement_PostCue(&Tab->Base, UIElementCue_Press);
            return (Result.Code == ErrorCode_Success) ? UIWorkspace_InternalPressTab(Screen, WorkspaceId, WindowId) : Result;
        }
        case UIMouseInputType_Move:
        {
            if ((args->Button != GameKey_MouseLeft) || !Tab->IsHeld || Tab->IsToggleOnRelease)
            {
                return Error_CreateSuccess();
            }
            bool IsTorn = false;
            return UIWorkspace_InternalDragTab(Screen, WorkspaceId, WindowId, args->Button, args->ScreenPosition, &IsTorn);
        }
        case UIMouseInputType_ButtonRelease:
        {
            Tab->HeldButtons = (uint8_t)(Tab->HeldButtons & (uint8_t)~Bit);
            if (args->Button != GameKey_MouseLeft)
            {
                return Error_CreateSuccess();
            }
            bool IsToggle = Tab->IsToggleOnRelease && Tab->IsHeld;
            Tab->IsHeld = false;
            Tab->IsToggleOnRelease = false;
            Error Result = UIElement_PostCue(&Tab->Base, UIElementCue_Release);
            return ((Result.Code == ErrorCode_Success) && IsToggle) ? UIWorkspace_InternalToggleMaximized(Screen, WorkspaceId, WindowId) : Result;
        }
        case UIMouseInputType_ReleaseOver:
            return Error_CreateSuccess();
    }
    return Error_CreateSuccess();
}

static Error Tab_OnPressLost(void* self, GameKey button)
{
    DockTab* Tab = self;
    Tab->HeldButtons = (uint8_t)(Tab->HeldButtons & (uint8_t)~UI_BUTTON_BIT(button));
    if (button == GameKey_MouseLeft)
    {
        Tab->IsHeld = false;
        Tab->IsToggleOnRelease = false;
    }
    return Error_CreateSuccess();
}

static Error Tab_OnHoverStart(void* self, const WidgetHoverArgs* args)
{
    (void)args;
    return UIElement_PostCue(&((DockTab*)self)->Base, UIElementCue_Hover);
}

/* Its window's name, the hover text of a tab that shows only its icon (and of the selected one too, cut or not). */
static Error WriteTabTooltip(UIScreen* screen, uint64_t widgetId, const UserData* userData, GenericBuffer* outText)
{
    (void)userData;
    Widget* Found = NULL;
    if (!UIScreen_TryGetWidgetById(screen, widgetId, &Found))
    {
        return Error_CreateSuccess();
    }
    const unsigned char* Title = UITextContent_Get(&((DockTab*)(void*)Found)->TitleContent);
    if ((Title != NULL) && !GenericBuffer_AppendString(outText, Title))
    {
        return Error_Construct2(ErrorCode_BufferTooSmall, "UIWorkspace: a tab's hover text could not be written.");
    }
    return Error_CreateSuccess();
}

/* Undock, Maximize and Close for the tab's window. */
static Error OpenTabMenu(UIScreen* screen, const UIContextMenuRequest* request, const UserData* userData)
{
    (void)userData;
    Widget* Found = NULL;
    if (!UIScreen_TryGetWidgetById(screen, request->ProviderWidgetId, &Found))
    {
        return Error_CreateSuccess();
    }
    DockTab* Tab = (DockTab*)(void*)Found;
    return UIWorkspace_InternalOpenTabMenu(screen, request, Tab->WorkspaceId, Tab->WindowId);
}


// Static functions: the splitter.
static Error Splitter_Construct(void* widget, void* args)
{
    DockSplitter* Splitter = widget;
    const UIDockSplitterArgs* Arguments = args;
    if (Arguments == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIWorkspace: a splitter needs its workspace and its split.");
    }
    Splitter->WorkspaceId = Arguments->WorkspaceId;
    Splitter->Split = Arguments->Split;
    Splitter->IsDragging = false;
    Error Result = UIElementLook_Construct(&Splitter->Look, UIElementKind_Workspace, NULL);
    // Dragging a splitter acts on nothing in the windows: the focus stays where it is.
    return (Result.Code == ErrorCode_Success) ? Widget_SetKeepsFocusOnPress(&Splitter->Base, true) : Result;
}

/* The pointer's shape by its split's direction, the state its look shows. */
static Error Splitter_Update(void* self, ProgramTime time)
{
    DockSplitter* Splitter = self;
    UIScreen* Screen = Widget_GetScreen(&Splitter->Base);
    const UIDockTree* Tree = UIWorkspace_GetTree(Screen, Splitter->WorkspaceId);
    const UIDockNode* Node = (Tree != NULL) ? UIDockTree_GetNode(Tree, Splitter->Split) : NULL;
    UICursorShape Shape = ((Node != NULL) && (Node->Split == UIDockSplit_Down)) ? UICursorShape_ResizeVertical : UICursorShape_ResizeHorizontal;
    Error Result = (Widget_GetCursorShape(&Splitter->Base) != Shape) ? Widget_SetCursorShape(&Splitter->Base, Shape) : Error_CreateSuccess();
    UIElementLook_Update(&Splitter->Look, Screen, UIElementState_Choose(true, Splitter->IsDragging, false, false, false,
        Widget_IsHovered(&Splitter->Base), false), time.PassedTime);
    return Result;
}

static Error Splitter_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)self;
    (void)context;
    (void)availableWidth;
    *outSize = (Vector2){ .x = 0.0f, .y = 0.0f }; // Its workspace places it.
    return Error_CreateSuccess();
}

static Error Splitter_Render(void* self, UIRenderContext* context)
{
    DockSplitter* Splitter = self;
    Rectangle Pixels = UIRenderContext_GetPixelBox(context);
    if (UIElement_IsBoxEmpty(Pixels))
    {
        return Error_CreateSuccess();
    }
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Splitter->Look, Widget_GetScreen(&Splitter->Base));
    return UIElementLook_DrawBox(&Splitter->Look, Style, UIWorkspacePart_Splitter, context, Pixels, Widget_GetEffectiveScale(&Splitter->Base));
}

/* A left press drags it: its split's ratio follows the pointer, each side kept at its windows' least size while there is room. */
static Error Splitter_OnMouseInput(void* self, const WidgetMouseInputArgs* args)
{
    DockSplitter* Splitter = self;
    if (args->Button != GameKey_MouseLeft)
    {
        return Error_CreateSuccess();
    }
    switch (args->Type)
    {
        case UIMouseInputType_ButtonPress:
            Splitter->IsDragging = true;
            return Error_CreateSuccess();
        case UIMouseInputType_Move:
            return Splitter->IsDragging ? UIWorkspace_InternalDragSplitter(Widget_GetScreen(&Splitter->Base), Splitter->WorkspaceId, Splitter->Split,
                args->ScreenPosition) : Error_CreateSuccess();
        case UIMouseInputType_ButtonRelease:
            Splitter->IsDragging = false;
            return Error_CreateSuccess();
        case UIMouseInputType_ReleaseOver:
            return Error_CreateSuccess();
    }
    return Error_CreateSuccess();
}

static Error Splitter_OnPressLost(void* self, GameKey button)
{
    DockSplitter* Splitter = self;
    if (button == GameKey_MouseLeft)
    {
        Splitter->IsDragging = false;
    }
    return Error_CreateSuccess();
}


// Static functions: the zones.
static Error Zones_Construct(void* widget, void* args)
{
    DockZones* Zones = widget;
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIWorkspace: what shows the drop zones needs its workspace.");
    }
    Zones->WorkspaceId = *(const uint64_t*)args;
    Zones->IsShowing = false;
    Memory_Zero(&Zones->View, sizeof(Zones->View));
    Error Result = UIElementLook_Construct(&Zones->Look, UIElementKind_Workspace, NULL);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    UIPlacement Whole = UIPlacement_Stretch(UIInsets_All(0.0f));
    return Widget_SetPlacement(widget, &Whole);
}

/* Goes with its workspace; otherwise reads where a drop would go now. */
static Error Zones_Update(void* self, ProgramTime time)
{
    DockZones* Zones = self;
    UIScreen* Screen = Widget_GetScreen(&Zones->Base);
    if (!UIWorkspace_IsWorkspace(Screen, Zones->WorkspaceId))
    {
        return Widget_Destroy(&Zones->Base);
    }
    Zones->IsShowing = UIWorkspace_GetDropView(Screen, Zones->WorkspaceId, &Zones->View);
    UIElementLook_Update(&Zones->Look, Screen, UIElementState_Normal, time.PassedTime);
    return Error_CreateSuccess();
}

static Error Zones_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)self;
    (void)context;
    (void)availableWidth;
    *outSize = (Vector2){ .x = 0.0f, .y = 0.0f };
    return Error_CreateSuccess();
}

static Rectangle RoundBox(Rectangle box)
{
    float Left = RoundToPixel(box.x);
    float Top = RoundToPixel(box.y);
    return (Rectangle){ .x = Left, .y = Top, .width = RoundToPixel(box.x + box.width) - Left, .height = RoundToPixel(box.y + box.height) - Top };
}

/* Where the window would go, then the area's guides over it (only the centre's over an empty dock), the one under the pointer hovered. */
static Error Zones_Render(void* self, UIRenderContext* context)
{
    DockZones* Zones = self;
    if (!Zones->IsShowing)
    {
        return Error_CreateSuccess();
    }
    const UIWorkspaceDropView* View = &Zones->View;
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Zones->Look, Widget_GetScreen(&Zones->Base));
    float Scale = Widget_GetEffectiveScale(&Zones->Base);
    Error Result = Error_CreateSuccess();
    Rectangle Preview = RoundBox(View->Preview);
    if (View->HasTarget && !UIElement_IsBoxEmpty(Preview))
    {
        Result = UIElementLook_DrawBox(&Zones->Look, Style, UIWorkspacePart_DropPreview, context, Preview, Scale);
    }
    const UIBoxStyle* Guide = UIThemeStyle_GetBox(Style, UIWorkspacePart_Zone);
    int32_t Count = View->IsDockEmpty ? 1 : (int32_t)UIDockZone_Count;
    for (int32_t Index = 0; (Index < Count) && (Guide != NULL) && (Result.Code == ErrorCode_Success); Index++)
    {
        bool IsUnder = View->HasTarget && (Index == (int32_t)View->Zone);
        UIBoxPaint Paint = UIBoxStyle_Resolve(Guide, IsUnder ? UIElementState_Hovered : UIElementState_Normal);
        Rectangle Box = UIDockZone_GetGuide(View->LeafBox, View->StripHeight, (UIDockZone)Index, View->GuideSize);
        Result = UIElement_IsBoxEmpty(Box) ? Error_CreateSuccess() : UIBoxStyle_Draw(Guide, context, Box, &Paint, Scale);
    }
    return Result;
}

static void* ResolveTabLook(void* widget)
{
    return &((DockTab*)widget)->Look;
}

static void* ResolveSplitterLook(void* widget)
{
    return &((DockSplitter*)widget)->Look;
}

static void* ResolveZonesLook(void* widget)
{
    return &((DockZones*)widget)->Look;
}


// Fields.
static const WidgetVTable TabVTable =
{
    .OnDeconstruct = Tab_OnDeconstruct,
    .Update = Tab_Update,
    .Render = Tab_Render,
    .OnHoverStart = Tab_OnHoverStart,
    .OnMouseInput = Tab_OnMouseInput,
    .OnPressLost = Tab_OnPressLost,
    .Measure = Tab_Measure
};

static const WidgetVTable SplitterVTable =
{
    .Update = Splitter_Update,
    .Render = Splitter_Render,
    .OnMouseInput = Splitter_OnMouseInput,
    .OnPressLost = Splitter_OnPressLost,
    .Measure = Splitter_Measure
};

static const WidgetVTable ZonesVTable =
{
    .Update = Zones_Update,
    .Render = Zones_Render,
    .Measure = Zones_Measure
};


// Internal functions (declared in UIWorkspaceInternal.h).
Error UIWorkspace_InternalRegisterParts(UIKit* kit, UIWidgetFactory* factory)
{
    uint64_t LookId = UIKit_GetCapabilityId(kit, UIElementCapability_Look);
    WidgetCapabilityEntry TabCapabilities[] = { { .CapabilityId = LookId, .Resolver = ResolveTabLook } };
    WidgetCapabilityEntry SplitterCapabilities[] = { { .CapabilityId = LookId, .Resolver = ResolveSplitterLook } };
    WidgetCapabilityEntry ZonesCapabilities[] = { { .CapabilityId = LookId, .Resolver = ResolveZonesLook } };
    WidgetTypeDescriptor Tab =
    {
        .Name = u8"DockTab",
        .StructSize = sizeof(DockTab),
        .VTable = &TabVTable,
        .Constructor = Tab_Construct,
        .Capabilities = TabCapabilities,
        .CapabilityCount = 1U,
        .IsFocusable = false
    };
    Error Result = UIWidgetFactory_RegisterType(factory, &Tab, &kit->_dockTabType);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    WidgetTypeDescriptor Splitter =
    {
        .Name = u8"DockSplitter",
        .StructSize = sizeof(DockSplitter),
        .VTable = &SplitterVTable,
        .Constructor = Splitter_Construct,
        .Capabilities = SplitterCapabilities,
        .CapabilityCount = 1U,
        .IsFocusable = false
    };
    Result = UIWidgetFactory_RegisterType(factory, &Splitter, &kit->_dockSplitterType);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    WidgetTypeDescriptor Zones =
    {
        .Name = u8"DockZones",
        .StructSize = sizeof(DockZones),
        .VTable = &ZonesVTable,
        .Constructor = Zones_Construct,
        .Capabilities = ZonesCapabilities,
        .CapabilityCount = 1U,
        .IsFocusable = false
    };
    return UIWidgetFactory_RegisterType(factory, &Zones, &kit->_dockZonesType);
}

Error UIWorkspace_InternalCreateTab(UIScreen* screen, uint64_t workspaceId, uint64_t windowId, uint64_t* outTabId)
{
    UIKit* Kit = UIKit_FromScreen(screen);
    UIDockTabArgs Arguments = { .Kit = Kit, .WorkspaceId = workspaceId, .WindowId = windowId };
    Error Result = CreatePart(screen, workspaceId, Kit->_dockTabType, &Arguments, outTabId);
    UserData Data = MakeIdData(workspaceId);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UITooltip_SetProvider(screen, *outTabId, WriteTabTooltip, &Data);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIContextMenu_SetProvider(screen, *outTabId, OpenTabMenu, &Data);
    }
    Widget* Made = NULL;
    if ((Result.Code == ErrorCode_Success) && UIScreen_TryGetWidgetById(screen, *outTabId, &Made))
    {
        Result = Widget_SetZLayer(Made, 1.0f); // Over the windows of its area: a tab is always pressed first.
    }
    if ((Result.Code != ErrorCode_Success) && UIScreen_TryGetWidgetById(screen, *outTabId, &Made))
    {
        Error Cleanup = Widget_Destroy(Made);
        Error_Deconstruct(&Cleanup);
        *outTabId = WIDGET_ID_INVALID;
    }
    return Result;
}

Error UIWorkspace_InternalCreateSplitter(UIScreen* screen, uint64_t workspaceId, int32_t split, uint64_t* outSplitterId)
{
    UIKit* Kit = UIKit_FromScreen(screen);
    UIDockSplitterArgs Arguments = { .WorkspaceId = workspaceId, .Split = split };
    Error Result = CreatePart(screen, workspaceId, Kit->_dockSplitterType, &Arguments, outSplitterId);
    Widget* Made = NULL;
    if ((Result.Code == ErrorCode_Success) && UIScreen_TryGetWidgetById(screen, *outSplitterId, &Made))
    {
        Result = Widget_SetZLayer(Made, 1.0f);
    }
    return Result;
}

Error UIWorkspace_InternalCreateZones(UIScreen* screen, uint64_t workspaceId, uint64_t* outZonesId)
{
    UIKit* Kit = UIKit_FromScreen(screen);
    uint64_t WorkspaceId = workspaceId;
    Error Result = CreatePart(screen, WIDGET_ID_INVALID, Kit->_dockZonesType, &WorkspaceId, outZonesId);
    Widget* Made = NULL;
    if ((Result.Code == ErrorCode_Success) && UIScreen_TryGetWidgetById(screen, *outZonesId, &Made))
    {
        Result = UIScreen_SetRootLayer(screen, Made, UIScreenLayer_Overlay);
    }
    return Result;
}
