#include "UIWorkspace.h"
#include "UIContextMenu.h"
#include "UILayouts.h"
#include "UIMenu.h"
#include "UIScreen.h"
#include "UIWindowTypes.h"
#include "UIWorkspaceInternal.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"


// Macros.
/** No window index. */
#define WINDOW_INDEX_NONE ((size_t)-1)
/** A tab's context menu's items. */
#define TAB_MENU_UNDOCK 1U
#define TAB_MENU_MAXIMIZE 2U
#define TAB_MENU_CLOSE 3U
/** How wide a torn-out window's title bar is grabbed from its left at most, in its screen's UI pixels. */
#define TORN_GRAB_OFFSET 40.0f


// Types.
/* What a leaf's minimum is worked out with, during the arrangement. */
typedef struct LeafMinimumDataStruct
{
    UIScreen* Screen;
    const UIWorkspaceCapability* Self;
    const UIMeasureContext* Sizing;
    float Scale;
    float Strip;
} LeafMinimumData;

/* What a tab's context menu acts on. */
typedef struct TabMenuDataStruct
{
    uint64_t WorkspaceId;
    uint64_t WindowId;
} TabMenuData;


// Static functions: helpers.
static UIWorkspaceCapability* GetCapability(void* widget)
{
    return &((UIWorkspaceLookBase*)widget)->Workspace;
}

static Error ResolveWorkspace(UIScreen* screen, uint64_t widgetId, Widget** outWidget, UIWorkspaceCapability** outWorkspace)
{
    UIKit* Kit = (screen != NULL) ? UIKit_FromScreen(screen) : NULL;
    Widget* Target = NULL;
    if ((Kit == NULL) || !UIScreen_TryGetWidgetById(screen, widgetId, &Target))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWorkspace: the widget is gone, or its screen has no kit.");
    }
    UIWorkspaceCapability* Workspace = Widget_GetCapability(Target, UIKit_GetCapabilityId(Kit, UIElementCapability_Workspace));
    if (Workspace == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWorkspace: the widget is not a workspace.");
    }
    *outWidget = Target;
    *outWorkspace = Workspace;
    return Error_CreateSuccess();
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

static float RoundToPixel(float value)
{
    return Math_FloorFloat(value + 0.5f);
}

static float CleanLength(float value)
{
    return (Math_IsNaNFloat(value) || Math_IsInfinityFloat(value) || !(value > 0.0f)) ? 0.0f : value;
}

static bool Contains(Rectangle box, Vector2 point)
{
    return (point.x >= box.x) && (point.y >= box.y) && (point.x < (box.x + box.width)) && (point.y < (box.y + box.height));
}

/* A point of the screen's [0;1] space in its surface pixels. */
static Vector2 ToPixels(const UIScreen* screen, Vector2 normalized)
{
    UIViewport Viewport = UIScreen_GetViewport(screen);
    return (Vector2){ .x = normalized.x * Viewport.SizePixels.x, .y = normalized.y * Viewport.SizePixels.y };
}

static size_t FindWindowIndex(const UIWorkspaceCapability* self, uint64_t windowId)
{
    for (size_t Index = 0U; Index < self->_windowCount; Index++)
    {
        if (self->_windows[Index].WindowId == windowId)
        {
            return Index;
        }
    }
    return WINDOW_INDEX_NONE;
}

static void RemoveRecordAt(UIWorkspaceCapability* self, size_t index)
{
    for (size_t Index = index; (Index + 1U) < self->_windowCount; Index++)
    {
        self->_windows[Index] = self->_windows[Index + 1U];
    }
    self->_windowCount--;
}

/* Places a child of the workspace at a box in surface pixels: its placement, in the workspace's own UI pixels from its top-left. Writing the
 * same placement changes nothing. Nothing for a part that is gone. */
static Error PlaceInPixels(UIScreen* screen, uint64_t id, Rectangle pixels, Rectangle parentPixels, float parentScale)
{
    Widget* Part = NULL;
    if (!UIScreen_TryGetWidgetById(screen, id, &Part) || !(parentScale > 0.0f))
    {
        return Error_CreateSuccess();
    }
    UIPlacement Placement = UIPlacement_FromTopLeft((pixels.x - parentPixels.x) / parentScale, (pixels.y - parentPixels.y) / parentScale,
        Math_MaxFloat(0.0f, pixels.width) / parentScale, Math_MaxFloat(0.0f, pixels.height) / parentScale);
    UIPlacement Current = Widget_GetPlacement(Part);
    if (Widget_HasPlacement(Part) && Memory_IsEqual(&Current, &Placement, sizeof(Placement)))
    {
        return Error_CreateSuccess();
    }
    return Widget_SetPlacement(Part, &Placement);
}

static Error DestroyIfAlive(UIScreen* screen, uint64_t id)
{
    Widget* Target = NULL;
    return UIScreen_TryGetWidgetById(screen, id, &Target) ? Widget_Destroy(Target) : Error_CreateSuccess();
}

/* A window shown or hidden: drawn, hit and updated with its subtree, or none of it. */
static Error ShowWindow(UIScreen* screen, uint64_t windowId, bool isShown)
{
    Widget* Window = NULL;
    if (!UIScreen_TryGetWidgetById(screen, windowId, &Window))
    {
        return Error_CreateSuccess();
    }
    Error Result = (Widget_IsRendered(Window) != isShown) ? Widget_SetIsRendered(Window, isShown) : Error_CreateSuccess();
    if ((Result.Code == ErrorCode_Success) && UIScreen_TryGetWidgetById(screen, windowId, &Window) && (Widget_IsUpdated(Window) != isShown))
    {
        Result = Widget_SetIsUpdated(Window, isShown);
    }
    return Result;
}

/* The content's box of a leaf of an arrangement: below its tab strip. */
static Rectangle GetContentBox(const UIWorkspaceCapability* self, int32_t leaf)
{
    Rectangle Box = self->_arrangement.NodeBoxes[leaf];
    float Strip = Math_MinFloat(self->_stripPixels, Box.height);
    return (Rectangle){ .x = Box.x, .y = Box.y + Strip, .width = Box.width, .height = Box.height - Strip };
}


// Static functions: making widgets follow the records.
/* Windows that are gone leave, with their tabs; tabs whose windows the workspace no longer keeps docked leave the dock; a record whose
 * window is in the dock is docked (or maximized out of it), and one whose window is not floats. */
static Error AgreeWithWindows(UIScreen* screen, uint64_t workspaceId)
{
    Widget* Root = NULL;
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, workspaceId, &Root);
    if (Self == NULL)
    {
        return Error_CreateSuccess();
    }
    for (size_t Index = 0U; Index < Self->_windowCount;)
    {
        UIWorkspaceWindow Record = Self->_windows[Index];
        if (UIWindow_IsWindow(screen, Record.WindowId))
        {
            Index++;
            continue;
        }
        Error Removed = UIDockTree_RemoveTab(&Self->_tree, Record.WindowId);
        Error_Deconstruct(&Removed); // A floating one has no tab.
        RemoveRecordAt(Self, Index);
        Error Result = DestroyIfAlive(screen, Record.TabId);
        Self = UIWorkspace_InternalFind(screen, workspaceId, &Root);
        if ((Result.Code != ErrorCode_Success) || (Self == NULL))
        {
            return Result;
        }
    }
    for (size_t Tab = 0U; Tab < UIDockTree_GetTotalTabCount(&Self->_tree);)
    {
        uint64_t Item = UI_DOCK_ITEM_NONE;
        (void)UIDockTree_GetTab(&Self->_tree, Tab, &Item, NULL);
        size_t Index = FindWindowIndex(Self, Item);
        if ((Index == WINDOW_INDEX_NONE) || (Self->_windows[Index].Place == UIWorkspacePlace_Floating))
        {
            Error Removed = UIDockTree_RemoveTab(&Self->_tree, Item);
            Error_Deconstruct(&Removed);
            continue;
        }
        Tab++;
    }
    UIDockTree_CollapseEmpty(&Self->_tree);
    for (size_t Index = 0U; Index < Self->_windowCount; Index++)
    {
        UIWorkspaceWindow* Record = &Self->_windows[Index];
        bool IsInDock = UIDockTree_FindTab(&Self->_tree, Record->WindowId, NULL, NULL);
        if (!IsInDock)
        {
            Record->Place = UIWorkspacePlace_Floating;
            continue;
        }
        bool IsMaximized = false;
        bool IsMinimized = false;
        Error Result = UIWindow_GetState(screen, Record->WindowId, &IsMinimized, &IsMaximized);
        Error_Deconstruct(&Result);
        // One maximized out of the dock that is so no more (its restore button) goes back to its tab.
        bool IsOut = (Record->Place == UIWorkspacePlace_Maximized) && IsMaximized && !IsMinimized;
        Record->Place = IsOut ? UIWorkspacePlace_Maximized : UIWorkspacePlace_Docked;
    }
    return Error_CreateSuccess();
}

/* A window back among the roots, in the layer its state calls for, in front. */
static Error MoveToRoots(UIScreen* screen, uint64_t windowId)
{
    Widget* Window = NULL;
    if (!UIScreen_TryGetWidgetById(screen, windowId, &Window) || Widget_IsScreenRoot(Window))
    {
        return Error_CreateSuccess();
    }
    Error Result = Widget_MoveTo(Window, NULL);
    bool IsMaximized = false;
    bool IsMinimized = false;
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWindow_GetState(screen, windowId, &IsMinimized, &IsMaximized);
    }
    if ((Result.Code == ErrorCode_Success) && UIScreen_TryGetWidgetById(screen, windowId, &Window))
    {
        Result = UIScreen_SetRootLayer(screen, Window, (IsMaximized && !IsMinimized) ? UIScreenLayer_Maximized : UIScreenLayer_Floating);
    }
    if ((Result.Code == ErrorCode_Success) && UIScreen_TryGetWidgetById(screen, windowId, &Window))
    {
        Result = UIScreen_BringToTop(screen, Window);
    }
    return Result;
}

/* A docked window: a child of the workspace, docked, shown and updated while its tab is selected, with its tab. */
static Error ApplyDocked(UIScreen* screen, uint64_t workspaceId, uint64_t windowId)
{
    Widget* Root = NULL;
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, workspaceId, &Root);
    Widget* Window = NULL;
    if ((Self == NULL) || !UIScreen_TryGetWidgetById(screen, windowId, &Window))
    {
        return Error_CreateSuccess();
    }
    Error Result = (Widget_GetParent(Window) != Root) ? Widget_MoveTo(Window, Root) : Error_CreateSuccess();
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWindow_SetDock(screen, windowId, UIWindowDock_Docked);
    }
    Self = UIWorkspace_InternalFind(screen, workspaceId, &Root);
    if ((Result.Code != ErrorCode_Success) || (Self == NULL))
    {
        return Result;
    }
    int32_t Leaf = UI_DOCK_NODE_NONE;
    bool IsSelected = UIDockTree_FindTab(&Self->_tree, windowId, &Leaf, NULL) && (UIDockTree_GetSelected(&Self->_tree, Leaf) == windowId);
    if (IsSelected && Self->_hasArrangement && UIDockTree_IsLeaf(&Self->_tree, Leaf))
    {
        // Where its layout will put it, at once: a window docked during the update does not show a frame where it was.
        Rectangle RootPixels = UIScreen_GetWidgetPixelBox(screen, Root);
        Result = PlaceInPixels(screen, windowId, GetContentBox(Self, Leaf), RootPixels, Widget_GetEffectiveScale(Root));
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ShowWindow(screen, windowId, IsSelected);
    }
    Self = UIWorkspace_InternalFind(screen, workspaceId, &Root);
    size_t Index = (Self != NULL) ? FindWindowIndex(Self, windowId) : WINDOW_INDEX_NONE;
    if ((Result.Code != ErrorCode_Success) || (Index == WINDOW_INDEX_NONE) || UIScreen_IsWidgetAlive(screen, Self->_windows[Index].TabId))
    {
        return Result;
    }
    uint64_t TabId = WIDGET_ID_INVALID;
    Result = UIWorkspace_InternalCreateTab(screen, workspaceId, windowId, &TabId);
    Self = UIWorkspace_InternalFind(screen, workspaceId, &Root);
    Index = (Self != NULL) ? FindWindowIndex(Self, windowId) : WINDOW_INDEX_NONE;
    if (Index != WINDOW_INDEX_NONE)
    {
        Self->_windows[Index].TabId = TabId;
    }
    return Result;
}

/* A window maximized out of the dock: a root, maximized, its tab kept. */
static Error ApplyMaximized(UIScreen* screen, uint64_t workspaceId, uint64_t windowId)
{
    Error Result = MoveToRoots(screen, windowId);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWindow_SetDock(screen, windowId, UIWindowDock_Maximized);
    }
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    if ((Result.Code == ErrorCode_Success) && (Self != NULL))
    {
        Result = ShowWindow(screen, windowId, Self->_isShown);
    }
    return Result;
}

/* A floating window: a root of its own with no tab, showing while the workspace does. */
static Error ApplyFloating(UIScreen* screen, uint64_t workspaceId, uint64_t windowId)
{
    Error Result = MoveToRoots(screen, windowId);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWindow_SetDock(screen, windowId, UIWindowDock_None);
    }
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    if ((Result.Code == ErrorCode_Success) && (Self != NULL))
    {
        Result = ShowWindow(screen, windowId, Self->_isShown);
    }
    Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    size_t Index = (Self != NULL) ? FindWindowIndex(Self, windowId) : WINDOW_INDEX_NONE;
    if ((Result.Code != ErrorCode_Success) || (Index == WINDOW_INDEX_NONE) || (Self->_windows[Index].TabId == WIDGET_ID_INVALID))
    {
        return Result;
    }
    uint64_t TabId = Self->_windows[Index].TabId;
    Self->_windows[Index].TabId = WIDGET_ID_INVALID;
    return DestroyIfAlive(screen, TabId);
}

/* A splitter per split, none elsewhere. */
static Error ReconcileSplitters(UIScreen* screen, uint64_t workspaceId)
{
    for (int32_t Node = 0; Node < (int32_t)UI_DOCK_TREE_MAX_NODES; Node++)
    {
        UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
        if (Self == NULL)
        {
            return Error_CreateSuccess();
        }
        bool IsSplit = UIDockTree_IsSplit(&Self->_tree, Node);
        bool HasSplitter = UIScreen_IsWidgetAlive(screen, Self->_splitterIds[Node]);
        if (IsSplit == HasSplitter)
        {
            continue;
        }
        Error Result = Error_CreateSuccess();
        if (HasSplitter)
        {
            uint64_t SplitterId = Self->_splitterIds[Node];
            Self->_splitterIds[Node] = WIDGET_ID_INVALID;
            Result = DestroyIfAlive(screen, SplitterId);
        }
        else
        {
            uint64_t SplitterId = WIDGET_ID_INVALID;
            Result = UIWorkspace_InternalCreateSplitter(screen, workspaceId, Node, &SplitterId);
            Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
            if (Self != NULL)
            {
                Self->_splitterIds[Node] = SplitterId;
            }
        }
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}


// Static functions: laying it out.
static void UpdateDrop(UIScreen* screen, UIWorkspaceCapability* self, uint64_t windowId, Vector2 screenPosition);

/* A leaf needs its tab strip and the largest least size of its windows. */
static Vector2 LeafMinimum(const UIDockTree* tree, int32_t leaf, const UserData* userData)
{
    const LeafMinimumData* Data = UserData_GetPointer(userData);
    Vector2 Minimum = { .x = 0.0f, .y = 0.0f };
    for (size_t Tab = 0U; Tab < UIDockTree_GetTabCount(tree, leaf); Tab++)
    {
        Widget* Window = NULL;
        if (!UIScreen_TryGetWidgetById(Data->Screen, UIDockTree_GetTabAt(tree, leaf, Tab), &Window))
        {
            continue;
        }
        Vector2 Least = { .x = 0.0f, .y = 0.0f };
        Error Result = Widget_Measure(Window, Data->Sizing, 0.0f, &Least);
        Error_Deconstruct(&Result); // A window that cannot say needs nothing.
        Minimum.x = Math_MaxFloat(Minimum.x, Least.x * Data->Scale);
        Minimum.y = Math_MaxFloat(Minimum.y, Least.y * Data->Scale);
    }
    Minimum.y += Data->Strip;
    return Minimum;
}

/* A leaf's tabs along its strip, left to right, each as wide as it measures (the selected one giving way first when they do not fit), cut
 * at the strip's end; and its windows in its content's box. */
static Error PlaceLeaf(UIScreen* screen, uint64_t workspaceId, int32_t leaf, Rectangle rootPixels, float scale, const UIMeasureContext* sizing)
{
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    if (Self == NULL)
    {
        return Error_CreateSuccess();
    }
    size_t Count = UIDockTree_GetTabCount(&Self->_tree, leaf);
    Rectangle Box = Self->_arrangement.NodeBoxes[leaf];
    Rectangle Strip = { .x = Box.x, .y = Box.y, .width = Box.width, .height = Math_MinFloat(Self->_stripPixels, Box.height) };
    Rectangle Content = GetContentBox(Self, leaf);
    float Spacing = RoundToPixel(Self->_metrics.TabSpacing * scale);
    float Widths[UI_DOCK_TREE_MAX_TABS];
    uint64_t Tabs[UI_DOCK_TREE_MAX_TABS];
    uint64_t Windows[UI_DOCK_TREE_MAX_TABS];
    size_t SelectedPlace = 0U;
    float Total = Spacing;
    for (size_t Place = 0U; Place < Count; Place++)
    {
        Windows[Place] = UIDockTree_GetTabAt(&Self->_tree, leaf, Place);
        UIWorkspaceWindow* Record = UIWorkspace_InternalFindWindow(Self, Windows[Place]);
        Tabs[Place] = (Record != NULL) ? Record->TabId : WIDGET_ID_INVALID;
        SelectedPlace = (Windows[Place] == UIDockTree_GetSelected(&Self->_tree, leaf)) ? Place : SelectedPlace;
        Widget* Tab = NULL;
        Vector2 Size = { .x = 0.0f, .y = 0.0f };
        if (UIScreen_TryGetWidgetById(screen, Tabs[Place], &Tab))
        {
            Error Result = Widget_Measure(Tab, sizing, UI_MEASURE_UNBOUNDED_WIDTH, &Size);
            Error_Deconstruct(&Result); // A tab that cannot say takes no room.
        }
        Widths[Place] = RoundToPixel(CleanLength(Size.x) * scale);
        Total += Widths[Place] + Spacing;
        // A window maximized out of the dock keeps its tab here, and is placed by itself.
        Windows[Place] = ((Record != NULL) && (Record->Place == UIWorkspacePlace_Docked)) ? Windows[Place] : WIDGET_ID_INVALID;
    }
    if ((Count > 0U) && (Total > Strip.width))
    {
        // The selected tab's name gives way first, down to its icon.
        float Least = RoundToPixel((Self->_metrics.TabPadding * 2.0f * scale) + Self->_stripPixels * 0.5f);
        float Shrunk = Math_MaxFloat(Least, Widths[SelectedPlace] - (Total - Strip.width));
        Widths[SelectedPlace] = Math_MinFloat(Widths[SelectedPlace], Shrunk);
    }
    // Still too many: the selected tab always shows, with as many before it as fit; the ones before those are left out (the wheel over
    // the strip steps through every tab).
    size_t First = 0U;
    if ((Count > 0U) && (Total > Strip.width))
    {
        float Needed = Spacing;
        for (size_t Place = 0U; Place <= SelectedPlace; Place++)
        {
            Needed += Widths[Place] + Spacing;
        }
        while ((First < SelectedPlace) && (Needed > Strip.width))
        {
            Needed -= Widths[First] + Spacing;
            First++;
        }
    }
    float X = Strip.x + Spacing;
    Error Result = Error_CreateSuccess();
    for (size_t Place = 0U; (Place < Count) && (Result.Code == ErrorCode_Success); Place++)
    {
        float Width = (Place < First) ? 0.0f : Math_MaxFloat(0.0f, Math_MinFloat(Widths[Place], (Strip.x + Strip.width) - X));
        Result = PlaceInPixels(screen, Tabs[Place], (Rectangle){ .x = X, .y = Strip.y, .width = Width, .height = Strip.height }, rootPixels, scale);
        X = (Place < First) ? X : Math_MinFloat(X + Width + Spacing, Strip.x + Strip.width);
        if (Result.Code == ErrorCode_Success)
        {
            Result = PlaceInPixels(screen, Windows[Place], Content, rootPixels, scale);
        }
    }
    return Result;
}

/* The bars along the top and the bottom at their heights, the screen's workspace left between them, the dock arranged there. */
static Error Behaviour_Layout(void* widget, const UILayoutContext* context)
{
    UIWorkspaceCapability* Self = GetCapability(widget);
    UIScreen* Screen = Widget_GetScreen(widget);
    uint64_t WorkspaceId = Widget_GetId(widget);
    Rectangle Pixels = UIScreen_GetWidgetPixelBox(Screen, widget);
    float Scale = Widget_GetEffectiveScale(widget);
    float Top = 0.0f;
    float Bottom = 0.0f;
    Error Result = Error_CreateSuccess();
    uint64_t Bars[2] = { Self->_menuBarId, Self->_statusBarId };
    for (int32_t Bar = 0; (Bar < 2) && (Result.Code == ErrorCode_Success); Bar++)
    {
        Widget* BarWidget = NULL;
        if (!UIScreen_TryGetWidgetById(Screen, Bars[Bar], &BarWidget) || !Widget_IsRendered(BarWidget))
        {
            continue;
        }
        Vector2 Size = { .x = 0.0f, .y = 0.0f };
        Result = Widget_Measure(BarWidget, context->Sizing, Pixels.width / Scale, &Size);
        float Height = Math_MinFloat(RoundToPixel(CleanLength(Size.y) * Scale), Math_MaxFloat(0.0f, Pixels.height - Top - Bottom));
        Rectangle Place = { .x = Pixels.x, .y = (Bar == 0) ? Pixels.y : (Pixels.y + Pixels.height - Height), .width = Pixels.width, .height = Height };
        if (Result.Code == ErrorCode_Success)
        {
            Result = PlaceInPixels(Screen, Bars[Bar], Place, Pixels, Scale);
        }
        Top += (Bar == 0) ? Height : 0.0f;
        Bottom += (Bar == 1) ? Height : 0.0f;
    }
    Self = UIWorkspace_InternalFind(Screen, WorkspaceId, NULL);
    if ((Result.Code != ErrorCode_Success) || (Self == NULL))
    {
        return Result;
    }
    float ScreenScale = UIScreen_GetScale(Screen);
    if (Self->_isShown && (ScreenScale > 0.0f))
    {
        UIViewport Viewport = UIScreen_GetViewport(Screen);
        UIInsets Wanted = { .Left = 0.0f, .Top = (Pixels.y + Top) / ScreenScale, .Right = 0.0f,
            .Bottom = Math_MaxFloat(0.0f, Viewport.SizePixels.y - (Pixels.y + Pixels.height - Bottom)) / ScreenScale };
        UIInsets Current = UIScreen_GetWorkspaceInsets(Screen);
        if (!Memory_IsEqual(&Current, &Wanted, sizeof(Wanted)))
        {
            Result = UIScreen_SetWorkspaceInsets(Screen, Wanted);
        }
    }
    Self->_dockArea = (Rectangle){ .x = Pixels.x, .y = Pixels.y + Top, .width = Pixels.width, .height = Math_MaxFloat(0.0f, Pixels.height - Top - Bottom) };
    Self->_stripPixels = RoundToPixel(Self->_metrics.TabHeight * Scale);
    Self->_guidePixels = RoundToPixel(Self->_metrics.GuideSize * Scale);
    LeafMinimumData Data = { .Screen = Screen, .Self = Self, .Sizing = context->Sizing, .Scale = Scale, .Strip = Self->_stripPixels };
    UserData MinimumData = UserData_FromPointer(&Data);
    UIDockTree_Arrange(&Self->_tree, Self->_dockArea, RoundToPixel(Self->_metrics.SplitterThickness * Scale), LeafMinimum, &MinimumData,
        &Self->_arrangement);
    Self->_hasArrangement = true;
    Self->_arrangedDock = UIWorkspace_InternalDockDigest(&Self->_tree);
    for (int32_t Node = 0; (Node < (int32_t)UI_DOCK_TREE_MAX_NODES) && (Result.Code == ErrorCode_Success); Node++)
    {
        Self = UIWorkspace_InternalFind(Screen, WorkspaceId, NULL);
        if (Self == NULL)
        {
            break;
        }
        if (UIDockTree_IsLeaf(&Self->_tree, Node))
        {
            Result = PlaceLeaf(Screen, WorkspaceId, Node, Pixels, Scale, context->Sizing);
        }
        else if (UIDockTree_IsSplit(&Self->_tree, Node))
        {
            Result = PlaceInPixels(Screen, Self->_splitterIds[Node], Self->_arrangement.SplitterBoxes[Node], Pixels, Scale);
        }
    }
    // A window dragged over the dock: where its drop would go, against the dock as laid out now (a tab torn out by this tick's move changed
    // it after that move was heard), so what the zones show and what a release does agree while the pointer rests.
    Self = UIWorkspace_InternalFind(Screen, WorkspaceId, NULL);
    if ((Result.Code == ErrorCode_Success) && (Self != NULL) && (Self->_dragWindowId != WIDGET_ID_INVALID))
    {
        UpdateDrop(Screen, Self, Self->_dragWindowId, UIScreen_GetPointerPosition(Screen));
    }
    return Result;
}


// Static functions: docking.
/* Docks a window it keeps beside a leaf: a tab at a place in it (the center), or a new leaf at one of its sides; in an empty dock, its first
 * leaf. The dock is unchanged when that cannot be (a limit): the window floats on. */
static Error DockAt(UIScreen* screen, uint64_t workspaceId, uint64_t windowId, int32_t leaf, UIDockZone zone, size_t place, bool* outIsDocked)
{
    *outIsDocked = false;
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    size_t Index = (Self != NULL) ? FindWindowIndex(Self, windowId) : WINDOW_INDEX_NONE;
    if (Index == WINDOW_INDEX_NONE)
    {
        return Error_CreateSuccess();
    }
    UIDockTree Before = Self->_tree;
    Error Removed = UIDockTree_RemoveTab(&Self->_tree, windowId);
    Error_Deconstruct(&Removed); // A floating one has no tab yet.
    Error Result = Error_CreateSuccess();
    if (UIDockTree_IsEmpty(&Self->_tree))
    {
        Result = UIDockTree_AddTab(&Self->_tree, UI_DOCK_NODE_NONE, windowId, 0U);
    }
    else if (zone == UIDockZone_Center)
    {
        Result = UIDockTree_AddTab(&Self->_tree, leaf, windowId, place);
    }
    else
    {
        Result = UIDockTree_SplitLeaf(&Self->_tree, leaf, zone, windowId, NULL);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Self->_tree = Before;
        Error_Deconstruct(&Result);
        return Error_CreateSuccess();
    }
    UIDockTree_CollapseEmpty(&Self->_tree);
    Self->_windows[Index].Place = UIWorkspacePlace_Docked;
    *outIsDocked = true;
    return UIWorkspace_InternalReconcile(screen, workspaceId);
}

/* Takes a docked window out of the dock: it floats at its floating box, in front. */
static Error UndockNow(UIScreen* screen, uint64_t workspaceId, uint64_t windowId)
{
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    size_t Index = (Self != NULL) ? FindWindowIndex(Self, windowId) : WINDOW_INDEX_NONE;
    if (Index == WINDOW_INDEX_NONE)
    {
        return Error_CreateSuccess();
    }
    Error Removed = UIDockTree_RemoveTab(&Self->_tree, windowId);
    Error_Deconstruct(&Removed);
    UIDockTree_CollapseEmpty(&Self->_tree);
    Self->_windows[Index].Place = UIWorkspacePlace_Floating;
    return UIWorkspace_InternalReconcile(screen, workspaceId);
}

/* Maximizes a docked window out of the dock, its tab keeping its place. */
static Error MaximizeOut(UIScreen* screen, uint64_t workspaceId, uint64_t windowId)
{
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    size_t Index = (Self != NULL) ? FindWindowIndex(Self, windowId) : WINDOW_INDEX_NONE;
    if ((Index == WINDOW_INDEX_NONE) || (Self->_windows[Index].Place != UIWorkspacePlace_Docked))
    {
        return Error_CreateSuccess();
    }
    Self->_windows[Index].Place = UIWorkspacePlace_Maximized;
    Error Result = ApplyMaximized(screen, workspaceId, windowId);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWindow_Maximize(screen, windowId);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWorkspace_InternalReconcile(screen, workspaceId);
    }
    return (Result.Code == ErrorCode_Success) ? UIWindow_Focus(screen, windowId) : Result;
}


// Static functions: the drag over the dock.
/* Whether another window above the dock (floating or maximized, showing) covers a point. */
static bool IsCoveredByOtherWindow(UIScreen* screen, uint64_t windowId, Vector2 pixels)
{
    for (size_t Index = 0U; Index < UIScreen_GetRootCount(screen); Index++)
    {
        Widget* Root = UIScreen_GetRootAt(screen, Index);
        if ((Root == NULL) || (Widget_GetId(Root) == windowId) || !Widget_IsRendered(Root) || !UIWindow_IsWindow(screen, Widget_GetId(Root)))
        {
            continue;
        }
        UIScreenLayer Layer = UIScreen_GetRootLayer(screen, Root);
        if (((Layer == UIScreenLayer_Floating) || (Layer == UIScreenLayer_Maximized)) && Contains(UIScreen_GetWidgetPixelBox(screen, Root), pixels))
        {
            return true;
        }
    }
    return false;
}

/* Where a drop of the dragged window would go now, from the pointer: an area of the dock and its zone, unless the workspace is hidden, the
 * pointer is off the dock, or another window covers it there. */
static void UpdateDrop(UIScreen* screen, UIWorkspaceCapability* self, uint64_t windowId, Vector2 screenPosition)
{
    self->_isOverDock = false;
    self->_hasDrop = false;
    self->_dropLeaf = UI_DOCK_NODE_NONE;
    Vector2 Pixels = ToPixels(screen, screenPosition);
    if (!self->_isShown || !self->_hasArrangement || !Contains(self->_dockArea, Pixels) || IsCoveredByOtherWindow(screen, windowId, Pixels))
    {
        return;
    }
    if (UIWorkspace_InternalDockDigest(&self->_tree) != self->_arrangedDock)
    {
        return; // The dock changed since it was laid out (a tab torn out this tick): no drop until it is laid out again.
    }
    self->_isOverDock = true;
    self->_hasDrop = UIDockTree_DropZoneAt(&self->_tree, &self->_arrangement, Pixels, self->_stripPixels, self->_guidePixels, &self->_dropLeaf,
        &self->_dropZone);
}

static void EndDrag(UIWorkspaceCapability* self)
{
    self->_dragWindowId = WIDGET_ID_INVALID;
    self->_isDragFromTab = false;
    self->_isOverDock = false;
    self->_hasDrop = false;
    self->_dropLeaf = UI_DOCK_NODE_NONE;
}

/* A window of the workspace being moved by its title bar: the zones follow the pointer over the dock, a release there docks it, Escape puts
 * a window torn out of a tab back. */
static Error OnWindowMove(UIScreen* screen, uint64_t windowId, UIWindowMoveStage stage, Vector2 screenPosition, const UserData* userData)
{
    uint64_t WorkspaceId = ReadIdData(userData);
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, WorkspaceId, NULL);
    if ((Self == NULL) || (FindWindowIndex(Self, windowId) == WINDOW_INDEX_NONE))
    {
        return Error_CreateSuccess();
    }
    if (stage == UIWindowMoveStage_Began)
    {
        Self->_dragWindowId = windowId;
        Self->_isDragFromTab = Self->_isTearingTab;
        Self->_isTearingTab = false;
        UpdateDrop(screen, Self, windowId, screenPosition);
        return Error_CreateSuccess();
    }
    if (Self->_dragWindowId != windowId)
    {
        return Error_CreateSuccess();
    }
    switch (stage)
    {
        case UIWindowMoveStage_Moved:
            UpdateDrop(screen, Self, windowId, screenPosition);
            return Error_CreateSuccess();
        case UIWindowMoveStage_Released:
        {
            UpdateDrop(screen, Self, windowId, screenPosition);
            bool HasDrop = Self->_hasDrop;
            int32_t Leaf = Self->_dropLeaf;
            UIDockZone Zone = Self->_dropZone;
            EndDrag(Self);
            bool IsDocked = false;
            return HasDrop ? DockAt(screen, WorkspaceId, windowId, Leaf, Zone, UI_DOCK_TREE_MAX_TABS, &IsDocked) : Error_CreateSuccess();
        }
        case UIWindowMoveStage_Cancelled:
        {
            bool IsFromTab = Self->_isDragFromTab;
            EndDrag(Self);
            if (!IsFromTab)
            {
                return Error_CreateSuccess();
            }
            // Back where the tab was: the dock as it was, the window's floating box too. A dock changed some other way meanwhile is kept:
            // the window floats where it was before the drag.
            bool IsDockAsTorn = UIWorkspace_InternalDockDigest(&Self->_tree) == Self->_dragTornDock;
            if (IsDockAsTorn)
            {
                Self->_tree = Self->_dragTree;
            }
            Rectangle Box = Self->_dragFloatingBox;
            Error Result = UIWindow_SetBox(screen, windowId, Box);
            Error_Deconstruct(&Result); // A box it had is a box it takes.
            Self = UIWorkspace_InternalFind(screen, WorkspaceId, NULL);
            size_t Index = (Self != NULL) ? FindWindowIndex(Self, windowId) : WINDOW_INDEX_NONE;
            if ((Index != WINDOW_INDEX_NONE) && IsDockAsTorn)
            {
                Self->_windows[Index].Place = UIWorkspacePlace_Docked;
            }
            return UIWorkspace_InternalReconcile(screen, WorkspaceId);
        }
        case UIWindowMoveStage_Lost:
            EndDrag(Self);
            return Error_CreateSuccess();
        case UIWindowMoveStage_Began:
            break;
    }
    return Error_CreateSuccess();
}

/* A tab's press handed to its window, which floats under the pointer: the window leaves the dock, the press goes on carrying it by its title
 * bar, and Escape can put it all back. */
static Error TearOut(UIScreen* screen, uint64_t workspaceId, uint64_t windowId, GameKey button, Vector2 pixels)
{
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    size_t Index = (Self != NULL) ? FindWindowIndex(Self, windowId) : WINDOW_INDEX_NONE;
    Widget* Window = NULL;
    if ((Index == WINDOW_INDEX_NONE) || !UIScreen_TryGetWidgetById(screen, windowId, &Window))
    {
        return Error_CreateSuccess();
    }
    Rectangle Box = { 0 };
    Error Result = UIWindow_GetBox(screen, windowId, &Box);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Self->_dragTree = Self->_tree;
    Self->_dragFloatingBox = Box;
    Error Removed = UIDockTree_RemoveTab(&Self->_tree, windowId);
    Error_Deconstruct(&Removed);
    UIDockTree_CollapseEmpty(&Self->_tree);
    Self->_dragTornDock = UIWorkspace_InternalDockDigest(&Self->_tree);
    Self->_windows[Index].Place = UIWorkspacePlace_Floating;
    uint64_t TabId = Self->_windows[Index].TabId;
    Self->_windows[Index].TabId = WIDGET_ID_INVALID;
    // Under the pointer, its title bar's middle on it, as far in from its left as the tab was grabbed (within reason).
    float ScreenScale = UIScreen_GetScale(screen);
    UIWindowCapability* Capability = Widget_GetCapability(Window, UIKit_GetCapabilityId(UIKit_FromScreen(screen), UIElementCapability_Window));
    float OwnScale = (ScreenScale > 0.0f) ? (Widget_GetEffectiveScale(Window) / ScreenScale) : 1.0f;
    float TitleHeight = (Capability != NULL) ? (Capability->_metrics.TitleHeight * OwnScale) : 0.0f;
    Rectangle Torn = Box;
    Torn.x = (pixels.x / ScreenScale) - Math_MinFloat(TORN_GRAB_OFFSET, Box.width * 0.5f);
    Torn.y = (pixels.y / ScreenScale) - Math_FloorFloat(TitleHeight * 0.5f);
    Result = UIWindow_SetBox(screen, windowId, Torn);
    if (Result.Code == ErrorCode_Success)
    {
        Result = ApplyFloating(screen, workspaceId, windowId);
    }
    Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    if ((Result.Code != ErrorCode_Success) || (Self == NULL))
    {
        return Result;
    }
    Self->_isTearingTab = true;
    Result = UIWindow_StartMove(screen, windowId, button);
    Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    if (Self != NULL)
    {
        Self->_isTearingTab = false;
    }
    Error TabResult = DestroyIfAlive(screen, TabId); // After the hand-over: the tab held the press.
    if (Result.Code == ErrorCode_Success)
    {
        Result = TabResult;
    }
    else
    {
        Error_Deconstruct(&TabResult);
    }
    return (Result.Code == ErrorCode_Success) ? UIWorkspace_InternalReconcile(screen, workspaceId) : Result;
}


// Static functions: the tab's context menu.
static Error OnTabMenu(UIScreen* screen, const UIMenuActivation* activation, const UserData* userData)
{
    TabMenuData Data;
    Memory_Copy(userData->Bytes, &Data, sizeof(Data));
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, Data.WorkspaceId, NULL);
    UIWorkspaceWindow* Record = (Self != NULL) ? UIWorkspace_InternalFindWindow(Self, Data.WindowId) : NULL;
    if ((Record == NULL) || (Record->Place != UIWorkspacePlace_Docked) || !UIWindow_IsWindow(screen, Data.WindowId))
    {
        return Error_CreateSuccess(); // Gone, or moved, since the menu opened.
    }
    switch (activation->ItemId)
    {
        case TAB_MENU_UNDOCK:
        {
            Error Result = UndockNow(screen, Data.WorkspaceId, Data.WindowId);
            return (Result.Code == ErrorCode_Success) ? UIWindow_Focus(screen, Data.WindowId) : Result;
        }
        case TAB_MENU_MAXIMIZE:
            return MaximizeOut(screen, Data.WorkspaceId, Data.WindowId);
        case TAB_MENU_CLOSE:
            return UIWindow_Close(screen, Data.WindowId, NULL);
        default:
            return Error_CreateSuccess();
    }
}


// Static functions: the behaviour.
static void* ResolveWorkspaceCapability(void* widget)
{
    return GetCapability(widget);
}

static Error Behaviour_Construct(void* widget, void* args)
{
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIWorkspace: the arguments are required.");
    }
    const UIWorkspaceOptions* Options = ((const UIWorkspaceArgs*)args)->Options;
    UIElementOptions Element = Options->Element;
    Element.HasPlacement = true;
    Element.Placement = UIPlacement_Stretch(UIInsets_All(0.0f)); // It covers its surface.
    Error Result = UIElement_ApplyOptions(widget, &Element);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    UIWorkspaceCapability* Self = GetCapability(widget);
    Memory_Zero(Self, sizeof(*Self));
    UIDockTree_Construct(&Self->_tree);
    UIDockTree_Construct(&Self->_dragTree);
    for (size_t Index = 0U; Index < (size_t)UI_DOCK_TREE_MAX_NODES; Index++)
    {
        Self->_splitterIds[Index] = WIDGET_ID_INVALID;
    }
    Self->_menuBarId = WIDGET_ID_INVALID;
    Self->_statusBarId = WIDGET_ID_INVALID;
    Self->_zonesId = WIDGET_ID_INVALID;
    Self->_dragWindowId = WIDGET_ID_INVALID;
    Self->_dropLeaf = UI_DOCK_NODE_NONE;
    Self->_dropZone = UIDockZone_Center;
    Self->_types = Options->Types;
    Self->_defaultLayoutData = UserData_CreateEmpty();
    Self->_switcherId = WIDGET_ID_INVALID;
    Self->_isShown = true;
    Self->_metrics = (UIWorkspaceMetrics){ .TabHeight = 22.0f, .TabPadding = 6.0f, .TabSpacing = 1.0f, .IconSpacing = 5.0f, .TabMaxWidth = 180.0f,
        .SplitterThickness = 4.0f, .GuideSize = 28.0f };
    // A press on its background, a bar's or a strip's keeps the focus where it is: what is being edited in a window stays so.
    return Widget_SetKeepsFocusOnPress(widget, true);
}

/* A pending switch, its widgets in step with its records, which areas show an active window, a drag whose window went, the layouts. */
static Error Behaviour_Update(void* widget, ProgramTime time)
{
    UIScreen* Screen = Widget_GetScreen(widget);
    uint64_t WorkspaceId = Widget_GetId(widget);
    Error Result = UIWorkspace_InternalReconcile(Screen, WorkspaceId);
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(Screen, WorkspaceId, NULL);
    if ((Result.Code != ErrorCode_Success) || (Self == NULL))
    {
        return Result;
    }
    if ((Self->_dragWindowId != WIDGET_ID_INVALID) && !UIScreen_IsWidgetAlive(Screen, Self->_dragWindowId))
    {
        EndDrag(Self); // Destroyed mid-drag: it tells nothing.
    }
    for (int32_t Node = 0; Node < (int32_t)UI_DOCK_TREE_MAX_NODES; Node++)
    {
        bool IsActive = false;
        uint64_t Shown = UIDockTree_GetSelected(&Self->_tree, Node);
        if (Shown != UI_DOCK_ITEM_NONE)
        {
            Error ActiveResult = UIWindow_IsActive(Screen, Shown, &IsActive);
            Error_Deconstruct(&ActiveResult); // Gone: not active.
        }
        Self->_isLeafActive[Node] = IsActive;
    }
    return UIWorkspace_InternalUpdateLayouts(Screen, WorkspaceId, time.TotalTime);
}


/* The wheel over an area's tab strip steps through its tabs as presses on them would (towards the player the next one, away the one
 * before), so a strip too short for its tabs reaches them all. */
static Error Behaviour_OnScrollInput(void* widget, const WidgetScrollInputArgs* args, bool* outIsHandled)
{
    UIWorkspaceCapability* Self = GetCapability(widget);
    UIScreen* Screen = Widget_GetScreen(widget);
    float Notches = (args->ScrollDelta.y != 0.0f) ? args->ScrollDelta.y : args->ScrollDelta.x;
    if (!Self->_hasArrangement || !(Math_AbsFloat(Notches) > 0.0f) || !(Math_AbsFloat(Notches) < UI_LAYOUT_MAX_LENGTH))
    {
        return Error_CreateSuccess();
    }
    Vector2 Pixels = ToPixels(Screen, args->ScreenPosition);
    for (int32_t Node = 0; Node < (int32_t)UI_DOCK_TREE_MAX_NODES; Node++)
    {
        Rectangle Box = Self->_arrangement.NodeBoxes[Node];
        Rectangle Strip = { .x = Box.x, .y = Box.y, .width = Box.width, .height = Math_MinFloat(Self->_stripPixels, Box.height) };
        if (!UIDockTree_IsLeaf(&Self->_tree, Node) || !Contains(Strip, Pixels))
        {
            continue;
        }
        *outIsHandled = true;
        size_t Count = UIDockTree_GetTabCount(&Self->_tree, Node);
        size_t Current = 0U;
        if ((Count == 0U) || !UIDockTree_FindTab(&Self->_tree, UIDockTree_GetSelected(&Self->_tree, Node), NULL, &Current))
        {
            return Error_CreateSuccess();
        }
        size_t Next = (Notches < 0.0f) ? Math_MinSizeT(Current + 1U, Count - 1U) : ((Current > 0U) ? (Current - 1U) : 0U);
        return (Next != Current) ? UIWorkspace_InternalPressTab(Screen, Widget_GetId(widget), UIDockTree_GetTabAt(&Self->_tree, Node, Next))
            : Error_CreateSuccess();
    }
    return Error_CreateSuccess();
}


// Fields.
static const UIKindCapability WorkspaceCapabilities[] =
{
    { .Capability = UIElementCapability_Workspace, .Resolve = ResolveWorkspaceCapability }
};

static const UIKindBehaviour WorkspaceBehaviour =
{
    .Kind = UIElementKind_Workspace,
    .Capabilities = WorkspaceCapabilities,
    .CapabilityCount = sizeof(WorkspaceCapabilities) / sizeof(WorkspaceCapabilities[0]),
    .BaseSize = sizeof(UIWorkspaceLookBase),
    .IsFocusable = false,
    .NeedsEntering = false,
    .EntersItself = false,
    .Construct = Behaviour_Construct,
    .Update = Behaviour_Update,
    .Layout = Behaviour_Layout,
    .OnScrollInput = Behaviour_OnScrollInput
};


// Internal functions (declared in UIKitInternal.h and UIWorkspaceInternal.h).
const UIKindBehaviour* UIWorkspace_GetKindBehaviour(void)
{
    return &WorkspaceBehaviour;
}

UIWorkspaceCapability* UIWorkspace_InternalFind(UIScreen* screen, uint64_t workspaceId, Widget** outWidget)
{
    Widget* Target = NULL;
    UIWorkspaceCapability* Workspace = NULL;
    Error Result = ResolveWorkspace(screen, workspaceId, &Target, &Workspace);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return NULL;
    }
    if (outWidget != NULL)
    {
        *outWidget = Target;
    }
    return Workspace;
}

UIWorkspaceWindow* UIWorkspace_InternalFindWindow(UIWorkspaceCapability* self, uint64_t windowId)
{
    size_t Index = FindWindowIndex(self, windowId);
    return (Index != WINDOW_INDEX_NONE) ? &self->_windows[Index] : NULL;
}

Error UIWorkspace_InternalReconcile(UIScreen* screen, uint64_t workspaceId)
{
    Error Result = AgreeWithWindows(screen, workspaceId);
    for (size_t Index = 0U; Result.Code == ErrorCode_Success; Index++)
    {
        UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
        if ((Self == NULL) || (Index >= Self->_windowCount))
        {
            break;
        }
        UIWorkspaceWindow Record = Self->_windows[Index];
        switch (Record.Place)
        {
            case UIWorkspacePlace_Docked:
                Result = ApplyDocked(screen, workspaceId, Record.WindowId);
                break;
            case UIWorkspacePlace_Maximized:
                Result = ApplyMaximized(screen, workspaceId, Record.WindowId);
                break;
            case UIWorkspacePlace_Floating:
                Result = ApplyFloating(screen, workspaceId, Record.WindowId);
                break;
        }
    }
    return (Result.Code == ErrorCode_Success) ? ReconcileSplitters(screen, workspaceId) : Result;
}

Error UIWorkspace_InternalPressTab(UIScreen* screen, uint64_t workspaceId, uint64_t windowId)
{
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    UIWorkspaceWindow* Record = (Self != NULL) ? UIWorkspace_InternalFindWindow(Self, windowId) : NULL;
    if ((Record == NULL) || (Record->Place != UIWorkspacePlace_Docked) || !UIWindow_IsWindow(screen, windowId))
    {
        return Error_CreateSuccess(); // Gone (closed since the workspace's last update): its tab leaves at the next one.
    }
    Error Result = UIDockTree_SelectTab(&Self->_tree, windowId);
    Error_Deconstruct(&Result); // A docked window has its tab.
    Result = UIWorkspace_InternalReconcile(screen, workspaceId);
    return (Result.Code == ErrorCode_Success) ? UIWindow_Focus(screen, windowId) : Result;
}

Error UIWorkspace_InternalDragTab(UIScreen* screen, uint64_t workspaceId, uint64_t windowId, GameKey button, Vector2 screenPosition, bool* outIsTorn)
{
    *outIsTorn = false;
    Widget* Root = NULL;
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, workspaceId, &Root);
    UIWorkspaceWindow* Record = (Self != NULL) ? UIWorkspace_InternalFindWindow(Self, windowId) : NULL;
    int32_t Leaf = UI_DOCK_NODE_NONE;
    if ((Record == NULL) || (Record->Place != UIWorkspacePlace_Docked) || !Self->_hasArrangement || !UIWindow_IsWindow(screen, windowId)
        || !UIDockTree_FindTab(&Self->_tree, windowId, &Leaf, NULL))
    {
        return Error_CreateSuccess();
    }
    Vector2 Pixels = ToPixels(screen, screenPosition);
    Rectangle Box = Self->_arrangement.NodeBoxes[Leaf];
    float Strip = Math_MinFloat(Self->_stripPixels, Box.height);
    float Distance = UI_WORKSPACE_UNDOCK_DISTANCE * Widget_GetEffectiveScale(Root);
    bool IsAway = (Pixels.y < (Box.y - Distance)) || (Pixels.y > (Box.y + Strip + Distance)) || (Pixels.x < (Box.x - Distance))
        || (Pixels.x > (Box.x + Box.width + Distance));
    if (IsAway)
    {
        *outIsTorn = true;
        return TearOut(screen, workspaceId, windowId, button, Pixels);
    }
    // Along the strip: its place among the other tabs is how many of them have their middle left of the pointer.
    size_t Place = 0U;
    for (size_t Tab = 0U; Tab < UIDockTree_GetTabCount(&Self->_tree, Leaf); Tab++)
    {
        uint64_t Other = UIDockTree_GetTabAt(&Self->_tree, Leaf, Tab);
        UIWorkspaceWindow* OtherRecord = UIWorkspace_InternalFindWindow(Self, Other);
        Widget* OtherTab = NULL;
        if ((Other == windowId) || (OtherRecord == NULL) || !UIScreen_TryGetWidgetById(screen, OtherRecord->TabId, &OtherTab))
        {
            continue;
        }
        Rectangle TabBox = UIScreen_GetWidgetPixelBox(screen, OtherTab);
        Place += ((TabBox.x + (TabBox.width * 0.5f)) < Pixels.x) ? 1U : 0U;
    }
    size_t Current = 0U;
    (void)UIDockTree_FindTab(&Self->_tree, windowId, NULL, &Current);
    if (Place == Current)
    {
        return Error_CreateSuccess();
    }
    Error Result = UIDockTree_MoveTab(&Self->_tree, windowId, Leaf, Place);
    Error_Deconstruct(&Result); // The window and its leaf are the tree's.
    return Error_CreateSuccess();
}

Error UIWorkspace_InternalDragSplitter(UIScreen* screen, uint64_t workspaceId, int32_t split, Vector2 screenPosition)
{
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    if ((Self == NULL) || !Self->_hasArrangement || !UIDockTree_IsSplit(&Self->_tree, split))
    {
        return Error_CreateSuccess();
    }
    float Ratio = UIDockTree_GetRatioAt(&Self->_tree, &Self->_arrangement, split, ToPixels(screen, screenPosition));
    Error Result = UIDockTree_SetRatio(&Self->_tree, split, Ratio);
    Error_Deconstruct(&Result); // A split and a finite ratio.
    return Error_CreateSuccess();
}

Error UIWorkspace_InternalOpenTabMenu(UIScreen* screen, const UIContextMenuRequest* request, uint64_t workspaceId, uint64_t windowId)
{
    UIMenuItem Items[3] =
    {
        UIMenuItem_Create(TAB_MENU_UNDOCK, u8"Undock"),
        UIMenuItem_Create(TAB_MENU_MAXIMIZE, u8"Maximize"),
        UIMenuItem_Create(TAB_MENU_CLOSE, u8"Close")
    };
    bool CanMaximize = false;
    Widget* Window = NULL;
    if (UIScreen_TryGetWidgetById(screen, windowId, &Window))
    {
        const UIWindowCapability* Capability = Widget_GetCapability(Window, UIKit_GetCapabilityId(UIKit_FromScreen(screen), UIElementCapability_Window));
        CanMaximize = (Capability != NULL) && Capability->_canMaximize;
        Items[2].IsEnabled = (Capability != NULL) && Capability->_canClose;
    }
    Items[1].IsEnabled = CanMaximize;
    TabMenuData Data = { .WorkspaceId = workspaceId, .WindowId = windowId };
    UserData MenuData = UserData_CreateEmpty();
    Memory_Copy(&Data, MenuData.Bytes, sizeof(Data));
    return UIMenu_OpenAt(screen, request, Items, sizeof(Items) / sizeof(Items[0]), OnTabMenu, &MenuData);
}

Error UIWorkspace_InternalToggleMaximized(UIScreen* screen, uint64_t workspaceId, uint64_t windowId)
{
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    UIWorkspaceWindow* Record = (Self != NULL) ? UIWorkspace_InternalFindWindow(Self, windowId) : NULL;
    if ((Record == NULL) || !UIWindow_IsWindow(screen, windowId))
    {
        return Error_CreateSuccess(); // Gone since the workspace's last update.
    }
    if (Record->Place == UIWorkspacePlace_Maximized)
    {
        Error Result = UIWindow_Restore(screen, windowId);
        return (Result.Code == ErrorCode_Success) ? UIWorkspace_InternalReconcile(screen, workspaceId) : Result;
    }
    const UIWindowCapability* Capability = NULL;
    Widget* Window = NULL;
    if (UIScreen_TryGetWidgetById(screen, windowId, &Window))
    {
        Capability = Widget_GetCapability(Window, UIKit_GetCapabilityId(UIKit_FromScreen(screen), UIElementCapability_Window));
    }
    return ((Capability != NULL) && Capability->_canMaximize) ? MaximizeOut(screen, workspaceId, windowId) : Error_CreateSuccess();
}

Error UIWorkspace_InternalOpenNew(UIScreen* screen, uint64_t workspaceId, uint32_t typeIndex, uint64_t* outWindowId)
{
    *outWindowId = WIDGET_ID_INVALID;
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    if ((Self == NULL) || (Self->_types == NULL) || (Self->_windowCount >= UI_WORKSPACE_MAX_WINDOWS))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWorkspace: no window types, or as many windows as a workspace keeps.");
    }
    uint64_t WindowId = WIDGET_ID_INVALID;
    Error Result = UIWindowTypes_OpenAt(Self->_types, screen, typeIndex, &WindowId);
    if ((Result.Code != ErrorCode_Success) || (WindowId == WIDGET_ID_INVALID))
    {
        return Result;
    }
    Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    if ((Self != NULL) && (FindWindowIndex(Self, WindowId) == WINDOW_INDEX_NONE))
    {
        Result = UIWorkspace_AdoptWindow(screen, workspaceId, WindowId);
    }
    *outWindowId = WindowId;
    return Result;
}

Error UIWorkspace_InternalSetParts(UIScreen* screen, uint64_t workspaceId, uint64_t menuBarId, uint64_t statusBarId, uint64_t zonesId)
{
    Widget* Root = NULL;
    UIWorkspaceCapability* Self = NULL;
    Error Result = ResolveWorkspace(screen, workspaceId, &Root, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (Self->_zonesId != WIDGET_ID_INVALID)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWorkspace: it has its parts already.");
    }
    Self->_menuBarId = menuBarId;
    Self->_statusBarId = statusBarId;
    Self->_zonesId = zonesId;
    return Error_CreateSuccess();
}


// Public functions: calling code.
bool UIWorkspace_IsWorkspace(UIScreen* screen, uint64_t widgetId)
{
    return UIWorkspace_InternalFind(screen, widgetId, NULL) != NULL;
}

Error UIWorkspace_GetMenuBarId(UIScreen* screen, uint64_t workspaceId, uint64_t* outMenuBarId)
{
    Widget* Root = NULL;
    UIWorkspaceCapability* Self = NULL;
    Error Result = ResolveWorkspace(screen, workspaceId, &Root, &Self);
    if (Result.Code == ErrorCode_Success)
    {
        *outMenuBarId = Self->_menuBarId;
    }
    return Result;
}

Error UIWorkspace_GetStatusBarId(UIScreen* screen, uint64_t workspaceId, uint64_t* outStatusBarId)
{
    Widget* Root = NULL;
    UIWorkspaceCapability* Self = NULL;
    Error Result = ResolveWorkspace(screen, workspaceId, &Root, &Self);
    if (Result.Code == ErrorCode_Success)
    {
        *outStatusBarId = Self->_statusBarId;
    }
    return Result;
}

Error UIWorkspace_SetShown(UIScreen* screen, uint64_t workspaceId, bool isShown)
{
    Widget* Root = NULL;
    UIWorkspaceCapability* Self = NULL;
    Error Result = ResolveWorkspace(screen, workspaceId, &Root, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Self->_isShown = isShown;
    if (!isShown)
    {
        EndDrag(Self);
    }
    uint64_t ZonesId = Self->_zonesId;
    Result = (Widget_IsRendered(Root) != isShown) ? Widget_SetIsRendered(Root, isShown) : Error_CreateSuccess();
    Widget* Zones = NULL;
    if ((Result.Code == ErrorCode_Success) && UIScreen_TryGetWidgetById(screen, ZonesId, &Zones) && (Widget_IsRendered(Zones) != isShown))
    {
        Result = Widget_SetIsRendered(Zones, isShown);
    }
    return (Result.Code == ErrorCode_Success) ? UIWorkspace_InternalReconcile(screen, workspaceId) : Result;
}

Error UIWorkspace_OpenType(UIScreen* screen, uint64_t workspaceId, uint32_t typeIndex, uint64_t* outWindowId)
{
    if (outWindowId != NULL)
    {
        *outWindowId = WIDGET_ID_INVALID;
    }
    Widget* Root = NULL;
    UIWorkspaceCapability* Self = NULL;
    Error Result = ResolveWorkspace(screen, workspaceId, &Root, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    const UIWindowType* Type = (Self->_types != NULL) ? UIWindowTypes_Get(Self->_types, typeIndex) : NULL;
    if (Type == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWorkspace_OpenType: no window types, or no active type at that index.");
    }
    // A single type's window it keeps comes back where it is.
    for (size_t Index = 0U; Type->_isSingle && (Index < Self->_windowCount); Index++)
    {
        UIWindowTypes* Types = NULL;
        uint32_t OwnIndex = 0U;
        Error TypeResult = UIWindow_GetType(screen, Self->_windows[Index].WindowId, &Types, &OwnIndex);
        Error_Deconstruct(&TypeResult);
        if ((Types != Self->_types) || (OwnIndex != typeIndex))
        {
            continue;
        }
        uint64_t WindowId = Self->_windows[Index].WindowId;
        if (Self->_windows[Index].Place == UIWorkspacePlace_Docked)
        {
            Result = UIWorkspace_InternalPressTab(screen, workspaceId, WindowId);
        }
        else
        {
            bool IsMinimized = false;
            Result = UIWindow_GetState(screen, WindowId, &IsMinimized, NULL);
            if ((Result.Code == ErrorCode_Success) && IsMinimized)
            {
                Result = UIWindow_Restore(screen, WindowId);
            }
            if (Result.Code == ErrorCode_Success)
            {
                Result = UIWindow_Focus(screen, WindowId);
            }
        }
        if ((Result.Code == ErrorCode_Success) && (outWindowId != NULL))
        {
            *outWindowId = WindowId;
        }
        return Result;
    }
    uint64_t WindowId = WIDGET_ID_INVALID;
    Result = UIWorkspace_InternalOpenNew(screen, workspaceId, typeIndex, &WindowId);
    if ((Result.Code == ErrorCode_Success) && (outWindowId != NULL))
    {
        *outWindowId = WindowId;
    }
    return Result;
}

Error UIWorkspace_OpenTypeByKey(UIScreen* screen, uint64_t workspaceId, const unsigned char* key, uint64_t* outWindowId)
{
    Widget* Root = NULL;
    UIWorkspaceCapability* Self = NULL;
    Error Result = ResolveWorkspace(screen, workspaceId, &Root, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    uint32_t Index = 0U;
    if ((key == NULL) || (Self->_types == NULL) || !UIWindowTypes_Find(Self->_types, key, &Index))
    {
        if (outWindowId != NULL)
        {
            *outWindowId = WIDGET_ID_INVALID;
        }
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWorkspace_OpenTypeByKey: no active window type has that key.");
    }
    return UIWorkspace_OpenType(screen, workspaceId, Index, outWindowId);
}

Error UIWorkspace_AdoptWindow(UIScreen* screen, uint64_t workspaceId, uint64_t windowId)
{
    Widget* Root = NULL;
    UIWorkspaceCapability* Self = NULL;
    Error Result = ResolveWorkspace(screen, workspaceId, &Root, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (FindWindowIndex(Self, windowId) != WINDOW_INDEX_NONE)
    {
        return Error_CreateSuccess();
    }
    Widget* Window = NULL;
    UIWindowDock Dock = UIWindowDock_None;
    Error DockResult = UIWindow_GetDock(screen, windowId, &Dock);
    bool IsWindow = DockResult.Code == ErrorCode_Success;
    Error_Deconstruct(&DockResult);
    if (!IsWindow || (Dock != UIWindowDock_None) || !UIScreen_TryGetWidgetById(screen, windowId, &Window) || !Widget_IsScreenRoot(Window)
        || (Self->_windowCount >= UI_WORKSPACE_MAX_WINDOWS))
    {
        return Error_Construct2(ErrorCode_InvalidOperation,
            "UIWorkspace_AdoptWindow: a floating window of no dock's is taken, while the workspace has room.");
    }
    Self->_windows[Self->_windowCount++] = (UIWorkspaceWindow){ .WindowId = windowId, .TabId = WIDGET_ID_INVALID, .Place = UIWorkspacePlace_Floating };
    UserData Data = MakeIdData(workspaceId);
    Result = UIWindow_SetMoveObserver(screen, windowId, OnWindowMove, &Data);
    return (Result.Code == ErrorCode_Success) ? UIWorkspace_InternalReconcile(screen, workspaceId) : Result;
}

Error UIWorkspace_Dock(UIScreen* screen, uint64_t workspaceId, uint64_t windowId, uint64_t besideWindowId, UIDockZone zone)
{
    Widget* Root = NULL;
    UIWorkspaceCapability* Self = NULL;
    Error Result = ResolveWorkspace(screen, workspaceId, &Root, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    int32_t Leaf = UI_DOCK_NODE_NONE;
    size_t Place = 0U;
    size_t Index = FindWindowIndex(Self, windowId);
    bool IsBesideDocked = (besideWindowId != WIDGET_ID_INVALID) && UIDockTree_FindTab(&Self->_tree, besideWindowId, &Leaf, &Place);
    bool IsWhereValid = IsBesideDocked ? (besideWindowId != windowId) : ((besideWindowId == WIDGET_ID_INVALID) && UIDockTree_IsEmpty(&Self->_tree));
    bool IsZoneValid = ((int32_t)zone >= 0) && ((int32_t)zone < (int32_t)UIDockZone_Count);
    if ((Index == WINDOW_INDEX_NONE) || !IsWhereValid || !IsZoneValid || (Self->_windows[Index].Place == UIWorkspacePlace_Maximized))
    {
        return Error_Construct2(ErrorCode_InvalidOperation,
            "UIWorkspace_Dock: a window it keeps, not maximized, beside another docked one (or into an empty dock).");
    }
    // Its place after its neighbour, counted once the window's own tab has left (it may stand before the neighbour in the same area).
    int32_t OwnLeaf = UI_DOCK_NODE_NONE;
    size_t OwnPlace = 0U;
    if (UIDockTree_FindTab(&Self->_tree, windowId, &OwnLeaf, &OwnPlace) && (OwnLeaf == Leaf) && (OwnPlace < Place))
    {
        Place--;
    }
    bool IsDocked = false;
    Result = DockAt(screen, workspaceId, windowId, Leaf, zone, Place + 1U, &IsDocked);
    if ((Result.Code == ErrorCode_Success) && !IsDocked)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWorkspace_Dock: the dock holds as many areas, tabs or levels as it can.");
    }
    return Result;
}

Error UIWorkspace_Undock(UIScreen* screen, uint64_t workspaceId, uint64_t windowId)
{
    Widget* Root = NULL;
    UIWorkspaceCapability* Self = NULL;
    Error Result = ResolveWorkspace(screen, workspaceId, &Root, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    UIWorkspaceWindow* Record = UIWorkspace_InternalFindWindow(Self, windowId);
    if ((Record == NULL) || (Record->Place == UIWorkspacePlace_Floating))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWorkspace_Undock: the workspace does not keep that window docked.");
    }
    if (Record->Place == UIWorkspacePlace_Maximized)
    {
        Result = UIWindow_Restore(screen, windowId);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UndockNow(screen, workspaceId, windowId);
    }
    return (Result.Code == ErrorCode_Success) ? UIWindow_Focus(screen, windowId) : Result;
}

Error UIWorkspace_MaximizeDocked(UIScreen* screen, uint64_t workspaceId, uint64_t windowId)
{
    Widget* Root = NULL;
    UIWorkspaceCapability* Self = NULL;
    Error Result = ResolveWorkspace(screen, workspaceId, &Root, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    UIWorkspaceWindow* Record = UIWorkspace_InternalFindWindow(Self, windowId);
    if ((Record == NULL) || (Record->Place != UIWorkspacePlace_Docked))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWorkspace_MaximizeDocked: the workspace does not keep that window docked.");
    }
    return MaximizeOut(screen, workspaceId, windowId);
}

Error UIWorkspace_SetAreaShare(UIScreen* screen, uint64_t workspaceId, uint64_t windowId, float share)
{
    Widget* Root = NULL;
    UIWorkspaceCapability* Self = NULL;
    Error Result = ResolveWorkspace(screen, workspaceId, &Root, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    int32_t Leaf = UI_DOCK_NODE_NONE;
    const UIDockNode* Node = UIDockTree_FindTab(&Self->_tree, windowId, &Leaf, NULL) ? UIDockTree_GetNode(&Self->_tree, Leaf) : NULL;
    const UIDockNode* Split = (Node != NULL) ? UIDockTree_GetNode(&Self->_tree, Node->Parent) : NULL;
    if (Split == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWorkspace_SetAreaShare: the window must be docked in an area a split divides.");
    }
    if (Math_IsNaNFloat(share) || Math_IsInfinityFloat(share))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIWorkspace_SetAreaShare: the share must be finite.");
    }
    float Kept = Math_ClampFloat(share, 0.0f, 1.0f);
    return UIDockTree_SetRatio(&Self->_tree, Node->Parent, (Split->Children[0] == Leaf) ? Kept : (1.0f - Kept));
}

Error UIWorkspace_SelectTab(UIScreen* screen, uint64_t workspaceId, uint64_t windowId)
{
    Widget* Root = NULL;
    UIWorkspaceCapability* Self = NULL;
    Error Result = ResolveWorkspace(screen, workspaceId, &Root, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    UIWorkspaceWindow* Record = UIWorkspace_InternalFindWindow(Self, windowId);
    if ((Record == NULL) || (Record->Place != UIWorkspacePlace_Docked))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWorkspace_SelectTab: the workspace does not keep that window docked.");
    }
    Result = UIDockTree_SelectTab(&Self->_tree, windowId);
    return (Result.Code == ErrorCode_Success) ? UIWorkspace_InternalReconcile(screen, workspaceId) : Result;
}

bool UIWorkspace_GetPlace(UIScreen* screen, uint64_t workspaceId, uint64_t windowId, UIWorkspacePlace* outPlace)
{
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    UIWorkspaceWindow* Record = (Self != NULL) ? UIWorkspace_InternalFindWindow(Self, windowId) : NULL;
    if (Record == NULL)
    {
        return false;
    }
    *outPlace = Record->Place;
    return true;
}

const UIDockTree* UIWorkspace_GetTree(UIScreen* screen, uint64_t workspaceId)
{
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    return (Self != NULL) ? &Self->_tree : NULL;
}

uint64_t UIWorkspace_GetTabId(UIScreen* screen, uint64_t workspaceId, uint64_t windowId)
{
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    UIWorkspaceWindow* Record = (Self != NULL) ? UIWorkspace_InternalFindWindow(Self, windowId) : NULL;
    return (Record != NULL) ? Record->TabId : WIDGET_ID_INVALID;
}

uint64_t UIWorkspace_GetSplitterId(UIScreen* screen, uint64_t workspaceId, int32_t split)
{
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    bool IsSplit = (Self != NULL) && UIDockTree_IsSplit(&Self->_tree, split);
    return IsSplit ? Self->_splitterIds[split] : WIDGET_ID_INVALID;
}

bool UIWorkspace_GetDropView(UIScreen* screen, uint64_t workspaceId, UIWorkspaceDropView* outView)
{
    UIWorkspaceCapability* Self = UIWorkspace_InternalFind(screen, workspaceId, NULL);
    if ((Self == NULL) || !Self->_isOverDock || (Self->_dragWindowId == WIDGET_ID_INVALID))
    {
        return false;
    }
    bool IsEmpty = UIDockTree_IsEmpty(&Self->_tree);
    if (!IsEmpty && !UIDockTree_IsLeaf(&Self->_tree, Self->_dropLeaf))
    {
        return false; // On a splitter.
    }
    Rectangle LeafBox = IsEmpty ? Self->_arrangement.Area : Self->_arrangement.NodeBoxes[Self->_dropLeaf];
    *outView = (UIWorkspaceDropView)
    {
        .LeafBox = LeafBox,
        .StripHeight = IsEmpty ? 0.0f : Self->_stripPixels,
        .GuideSize = Self->_guidePixels,
        .IsDockEmpty = IsEmpty,
        .HasTarget = Self->_hasDrop,
        .Zone = Self->_dropZone,
        .Preview = UIDockZone_GetPreview(LeafBox, Self->_dropZone, Self->_arrangement.SplitterThickness)
    };
    return true;
}


// Public functions: behaviour helpers.
void UIWorkspaceCapability_SetMetrics(UIWorkspaceCapability* self, Widget* widget, const UIWorkspaceMetrics* metrics)
{
    (void)widget;
    UIWorkspaceMetrics Clean =
    {
        .TabHeight = Math_MaxFloat(CleanLength(metrics->TabHeight), UI_WINDOW_MIN_TITLE_HEIGHT),
        .TabPadding = CleanLength(metrics->TabPadding),
        .TabSpacing = CleanLength(metrics->TabSpacing),
        .IconSpacing = CleanLength(metrics->IconSpacing),
        .TabMaxWidth = CleanLength(metrics->TabMaxWidth),
        .SplitterThickness = CleanLength(metrics->SplitterThickness),
        .GuideSize = CleanLength(metrics->GuideSize)
    };
    self->_metrics = Clean;
}

bool UIWorkspaceCapability_GetLeaf(const UIWorkspaceCapability* self, int32_t leaf, Rectangle* outStrip, bool* outIsActive)
{
    if (!self->_hasArrangement || !UIDockTree_IsLeaf(&self->_tree, leaf))
    {
        return false;
    }
    Rectangle Box = self->_arrangement.NodeBoxes[leaf];
    *outStrip = (Rectangle){ .x = Box.x, .y = Box.y, .width = Box.width, .height = Math_MinFloat(self->_stripPixels, Box.height) };
    *outIsActive = self->_isLeafActive[leaf];
    return true;
}

Error UIWorkspace_RegisterTypes(UIKit* kit, UIWidgetFactory* factory)
{
    return UIWorkspace_InternalRegisterParts(kit, factory);
}
