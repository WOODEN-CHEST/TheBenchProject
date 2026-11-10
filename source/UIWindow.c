#include "UIWindow.h"
#include "Renderer.h"
#include "UIButton.h"
#include "UIKit.h"
#include "UIKitInternal.h"
#include "UIPopup.h"
#include "UIScreen.h"
#include "UIScrollArea.h"
#include "UIScrollbar.h"
#include "UIWidgetFactory.h"
#include "UIWindowTypes.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"


// Macros.
/** The largest share of a window's shorter side a corner's grip takes: two corners always leave a stretch of edge between them. */
#define CORNER_SHARE 0.25f
/** A corner's grip is this many times an edge's band, so a corner is easy to catch. */
#define CORNER_MARGINS 2.0f


// Types.
/* The frame: a part under all the others, as large as the window. A press on what shows of it (the border) gives the window the focus
 * as one on the title bar does, keeping the focus where it is on the press. It draws nothing (the window's look draws the frame). */
typedef struct UIWindowFrameStruct
{
    Widget Base;
    uint64_t WindowId;
} UIWindowFrame;

/* The title bar: the part of a window's frame a press moves it by. It draws nothing (the window's look draws it) and keeps the focus where
 * it is on a press: the window gives the focus back itself, so the press acts on nothing inside. Its children are the caption buttons. */
typedef struct UIWindowTitleBarStruct
{
    Widget Base;
    uint64_t WindowId;
} UIWindowTitleBar;

/* An edge or a corner a press resizes the window by: a band along the frame, on top of the content, drawing nothing (the window's look
 * draws its highlight) and keeping the focus where it is. */
typedef struct UIWindowEdgeWidgetStruct
{
    Widget Base;
    uint64_t WindowId;
    UIWindowEdge Edge;
} UIWindowEdgeWidget;



// Static functions: helpers.
static UIWindowCapability* GetCapability(void* widget)
{
    return &((UIWindowLookBase*)widget)->Window;
}

static Error ResolveWindow(UIScreen* screen, uint64_t widgetId, Widget** outWidget, UIWindowCapability** outWindow)
{
    UIKit* Kit = (screen != NULL) ? UIKit_FromScreen(screen) : NULL;
    Widget* Target = NULL;
    if ((Kit == NULL) || !UIScreen_TryGetWidgetById(screen, widgetId, &Target))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWindow: the widget is gone, or its screen has no kit.");
    }
    UIWindowCapability* Window = Widget_GetCapability(Target, UIKit_GetCapabilityId(Kit, UIElementCapability_Window));
    if (Window == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWindow: the widget is not a window.");
    }
    *outWidget = Target;
    *outWindow = Window;
    return Error_CreateSuccess();
}

/* The window's capability, or NULL for a widget that is gone or no window: for the paths that have nothing to do then. */
static UIWindowCapability* FindWindow(UIScreen* screen, uint64_t widgetId, Widget** outWidget)
{
    Widget* Target = NULL;
    UIWindowCapability* Window = NULL;
    Error Result = ResolveWindow(screen, widgetId, &Target, &Window);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return NULL;
    }
    if (outWidget != NULL)
    {
        *outWidget = Target;
    }
    return Window;
}

static float CleanLength(float value)
{
    return (Math_IsNaNFloat(value) || Math_IsInfinityFloat(value) || !(value > 0.0f)) ? 0.0f : value;
}

static bool IsFiniteFloat(float value)
{
    return !Math_IsNaNFloat(value) && !Math_IsInfinityFloat(value);
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

/* How much larger than its screen's UI pixels one of a root window's own is: its own scale, as its effective scale has it. */
static float GetOwnScale(const Widget* widget)
{
    float ScreenScale = UIScreen_GetScale(Widget_GetScreen(widget));
    float Effective = Widget_GetEffectiveScale(widget);
    return ((ScreenScale > 0.0f) && (Effective > 0.0f)) ? (Effective / ScreenScale) : 1.0f;
}

/* A point of the screen's [0;1] space in its UI pixels. */
static Vector2 ToScreenUnits(const UIScreen* screen, Vector2 normalized)
{
    UIViewport Viewport = UIScreen_GetViewport(screen);
    float Scale = UIScreen_GetScale(screen);
    return (Vector2){ .x = (normalized.x * Viewport.SizePixels.x) / Scale, .y = (normalized.y * Viewport.SizePixels.y) / Scale };
}

/* A measuring context for the screen's viewport, for measuring outside the layout pass (as the popup layer does). */
static void MakeSizing(const UIScreen* screen, RenderContext* outMeasure, UIMeasureContext* outSizing)
{
    UIViewport Viewport = UIScreen_GetViewport(screen);
    RenderContext_CreateMeasuring(outMeasure, Viewport.SizePixels, Viewport.TargetAspectRatio, Viewport.TargetRelativePosition);
    outSizing->Measure = outMeasure;
}

/* Whether a widget lies inside another (not the same one). */
static bool IsInside(const Widget* widget, const Widget* ancestor)
{
    const Widget* Current = (widget != NULL) ? Widget_GetParent(widget) : NULL;
    for (size_t Step = 0U; (Step < WIDGET_MAX_TREE_DEPTH) && (Current != NULL); Step++)
    {
        if (Current == ancestor)
        {
            return true;
        }
        Current = Widget_GetParent(Current);
    }
    return false;
}

/* Whether a widget shows and is part of its screen's tree: it and every ancestor rendered, up to a root of the screen. */
static bool IsShownAndAttached(const Widget* widget)
{
    const Widget* Current = widget;
    for (size_t Step = 0U; (Step <= WIDGET_MAX_TREE_DEPTH) && (Current != NULL); Step++)
    {
        if (!Widget_IsRendered(Current))
        {
            return false;
        }
        if (Widget_IsScreenRoot(Current))
        {
            return true;
        }
        Current = Widget_GetParent(Current);
    }
    return false;
}

/* Whether the focus can go to a widget: alive, the window or inside it, focusable, taking input, shown and attached. */
static bool CanTakeFocus(UIScreen* screen, uint64_t widgetId, const Widget* window)
{
    Widget* Candidate = NULL;
    if ((widgetId == WIDGET_ID_INVALID) || !UIScreen_TryGetWidgetById(screen, widgetId, &Candidate))
    {
        return false;
    }
    bool IsInWindow = (Candidate == window) || IsInside(Candidate, window);
    return IsInWindow && Widget_IsFocusable(Candidate) && Widget_IsInputEnabled(Candidate) && IsShownAndAttached(Candidate);
}

/* Whether a caption button shows: the minimize one while a root window of its own can minimize (and while minimized, as restore), the
 * maximize one while a root window can maximize and is not minimized, the close one while it can close. A window another widget places
 * neither minimizes nor maximizes, one maximized out of its dock does not minimize, and a docked one has no title bar to show them in. */
static bool IsButtonShown(const UIWindowCapability* self, UIWindowButton button, bool isRoot)
{
    if (self->_dock == UIWindowDock_Docked)
    {
        return false;
    }
    switch (button)
    {
        case UIWindowButton_Minimize:
            return (self->_canMinimize && isRoot && (self->_dock == UIWindowDock_None)) || self->_isMinimized;
        case UIWindowButton_Maximize:
            return self->_canMaximize && !self->_isMinimized && isRoot;
        case UIWindowButton_Close:
            return self->_canClose;
        case UIWindowButton_Count:
            return false;
    }
    return false;
}

/* The letter a caption button shows for the window's state. */
static uint32_t GetButtonLetter(const UIWindowCapability* self, UIWindowButton button)
{
    switch (button)
    {
        case UIWindowButton_Minimize:
            return self->_isMinimized ? UI_WINDOW_RESTORE_LETTER : UI_WINDOW_MINIMIZE_LETTER;
        case UIWindowButton_Maximize:
            return self->_isMaximized ? UI_WINDOW_RESTORE_LETTER : UI_WINDOW_MAXIMIZE_LETTER;
        case UIWindowButton_Close:
        case UIWindowButton_Count:
            return UI_WINDOW_CLOSE_LETTER;
    }
    return UI_WINDOW_CLOSE_LETTER;
}

/* How much of the title bar's right end the caption buttons take, the title bar's padding there included, in its own UI pixels. */
static float GetButtonsWidth(const UIWindowCapability* self, bool isRoot)
{
    float Width = self->_metrics.TitlePadding;
    bool HasOne = false;
    for (int32_t Button = 0; Button < (int32_t)UIWindowButton_Count; Button++)
    {
        if (IsButtonShown(self, (UIWindowButton)Button, isRoot))
        {
            Width += self->_metrics.ButtonSize.x + (HasOne ? self->_metrics.ButtonSpacing : 0.0f);
            HasOne = true;
        }
    }
    return Width;
}

/* The title bar's height and the frame's border: none while docked (its tab stands for its title bar, its dock's frame for its own). */
static float GetTitleHeight(const UIWindowCapability* self)
{
    return (self->_dock == UIWindowDock_Docked) ? 0.0f : self->_metrics.TitleHeight;
}

static float GetBorder(const UIWindowCapability* self)
{
    return (self->_dock == UIWindowDock_Docked) ? 0.0f : self->_metrics.Border;
}

/* What the title bar needs across at least: its padding, the icon and its spacing, a gap before the buttons, and the buttons; nothing while
 * it has none. */
static float GetTitleBarNeeds(const UIWindowCapability* self, bool isRoot)
{
    if (self->_dock == UIWindowDock_Docked)
    {
        return 0.0f;
    }
    const UIWindowMetrics* Metrics = &self->_metrics;
    float Icon = (Metrics->IconWidth > 0.0f) ? (Metrics->IconWidth + Metrics->IconSpacing) : 0.0f;
    return Metrics->TitlePadding + Icon + Metrics->ButtonSpacing + GetButtonsWidth(self, isRoot);
}

/* An edge's band in a window's box of a size, in its own UI pixels from its top-left. */
static Rectangle GetEdgeBand(UIWindowEdge edge, float width, float height, float margin)
{
    float Corner = Math_MinFloat(CORNER_MARGINS * margin, Math_MinFloat(width, height) * CORNER_SHARE);
    float Across = Math_MaxFloat(0.0f, width - (2.0f * Corner));
    float Down = Math_MaxFloat(0.0f, height - (2.0f * Corner));
    switch (edge)
    {
        case UIWindowEdge_Left:
            return (Rectangle){ .x = 0.0f, .y = Corner, .width = margin, .height = Down };
        case UIWindowEdge_Top:
            return (Rectangle){ .x = Corner, .y = 0.0f, .width = Across, .height = margin };
        case UIWindowEdge_Right:
            return (Rectangle){ .x = width - margin, .y = Corner, .width = margin, .height = Down };
        case UIWindowEdge_Bottom:
            return (Rectangle){ .x = Corner, .y = height - margin, .width = Across, .height = margin };
        case UIWindowEdge_TopLeft:
            return (Rectangle){ .x = 0.0f, .y = 0.0f, .width = Corner, .height = Corner };
        case UIWindowEdge_TopRight:
            return (Rectangle){ .x = width - Corner, .y = 0.0f, .width = Corner, .height = Corner };
        case UIWindowEdge_BottomLeft:
            return (Rectangle){ .x = 0.0f, .y = height - Corner, .width = Corner, .height = Corner };
        case UIWindowEdge_BottomRight:
            return (Rectangle){ .x = width - Corner, .y = height - Corner, .width = Corner, .height = Corner };
        case UIWindowEdge_Count:
            break;
    }
    return (Rectangle){ .x = 0.0f, .y = 0.0f, .width = 0.0f, .height = 0.0f };
}

/* The pointer's shape over an edge: which way it resizes. */
static UICursorShape GetEdgeShape(UIWindowEdge edge)
{
    switch (edge)
    {
        case UIWindowEdge_Left:
        case UIWindowEdge_Right:
            return UICursorShape_ResizeHorizontal;
        case UIWindowEdge_Top:
        case UIWindowEdge_Bottom:
            return UICursorShape_ResizeVertical;
        case UIWindowEdge_TopLeft:
        case UIWindowEdge_BottomRight:
            return UICursorShape_ResizeDiagonalDown;
        case UIWindowEdge_TopRight:
        case UIWindowEdge_BottomLeft:
            return UICursorShape_ResizeDiagonalUp;
        case UIWindowEdge_Count:
            break;
    }
    return UICursorShape_Arrow;
}

/* A part's box in its parent's, in the parent's UI pixels; nothing for a part that is gone. Writing the same placement changes nothing. */
static Error PlaceAt(UIScreen* screen, uint64_t id, Rectangle box)
{
    Widget* Part = NULL;
    if (!UIScreen_TryGetWidgetById(screen, id, &Part))
    {
        return Error_CreateSuccess();
    }
    UIPlacement Placement = UIPlacement_FromTopLeft(box.x, box.y, Math_MaxFloat(0.0f, box.width), Math_MaxFloat(0.0f, box.height));
    return Widget_SetPlacement(Part, &Placement);
}

/* A bar's thickness across, in its parent's UI pixels; 0 for one that is gone. */
static float MeasureBar(UIScreen* screen, uint64_t areaId, UIScrollAxis axis, const UIMeasureContext* context)
{
    Widget* Area = NULL;
    if (!UIScreen_TryGetWidgetById(screen, areaId, &Area))
    {
        return 0.0f;
    }
    // Every scroll area's struct starts with UIScrollAreaLookBase (UIKit_RegisterLook checks its size).
    uint64_t BarId = ((UIScrollAreaLookBase*)(void*)Area)->Area._barIds[axis];
    float Thickness = 0.0f;
    Error Result = UIScrollbar_MeasureThickness(screen, BarId, axis, context, &Thickness);
    Error_Deconstruct(&Result); // A bar that cannot say takes no room.
    return Thickness;
}

/* A root window's least size at a width, in its screen's UI pixels (its Measure, cached until something in it changes). */
static Error MeasureLeast(Widget* widget, float width, Vector2* outLeast)
{
    RenderContext Measure;
    UIMeasureContext Sizing;
    MakeSizing(Widget_GetScreen(widget), &Measure, &Sizing);
    return Widget_Measure(widget, &Sizing, width, outLeast);
}


// Static functions: the focus.
/* Follows the focus: the window is active while it is in it, or in a popup opened from inside it (a menu or a list, or a popup opened
 * from one of those); what last had it inside is remembered entered or not. */
static void TrackFocus(Widget* widget, UIWindowCapability* self)
{
    UIScreen* Screen = Widget_GetScreen(widget);
    uint64_t FocusedId = UIScreen_GetFocusedWidgetId(Screen);
    // In a popup, what opened the popup stands for it.
    uint64_t StandsFor = FocusedId;
    for (size_t Step = 0U; (Step <= UIScreen_GetPopupCount(Screen)) && (StandsFor != WIDGET_ID_INVALID); Step++)
    {
        uint64_t PopupId = UIScreen_FindPopupOf(Screen, StandsFor);
        if (PopupId == WIDGET_ID_INVALID)
        {
            break;
        }
        StandsFor = UIScreen_GetPopupOwner(Screen, PopupId);
    }
    Widget* Focused = NULL;
    bool IsFound = (StandsFor != WIDGET_ID_INVALID) && UIScreen_TryGetWidgetById(Screen, StandsFor, &Focused);
    bool IsWithin = IsFound && IsInside(Focused, widget);
    self->_isActive = IsWithin || (IsFound && (Focused == widget));
    if (IsWithin && (StandsFor == FocusedId))
    {
        self->_lastFocusId = FocusedId;
        self->_wasLastFocusEntered = UIScreen_GetEnteredWidgetId(Screen) == FocusedId;
    }
}

/* Gives a window the focus as a press on its title bar does: nothing when it is inside the window already; else (from elsewhere, or on
 * the window itself) to what last had it inside (entered again if it was, on a screen that has the keyboard), else to its default focus
 * target (entered if asked), else to the window itself. */
static Error ActivateWindow(UIScreen* screen, uint64_t windowId)
{
    Widget* Window = NULL;
    UIWindowCapability* Self = FindWindow(screen, windowId, &Window);
    if (Self == NULL)
    {
        return Error_CreateSuccess();
    }
    Widget* Focused = NULL;
    uint64_t FocusedId = UIScreen_GetFocusedWidgetId(screen);
    if (UIScreen_TryGetWidgetById(screen, FocusedId, &Focused) && IsInside(Focused, Window))
    {
        return Error_CreateSuccess();
    }
    uint64_t TargetId = Self->_lastFocusId;
    bool IsEntered = Self->_wasLastFocusEntered;
    if (!CanTakeFocus(screen, TargetId, Window))
    {
        TargetId = Self->_defaultFocusId;
        IsEntered = Self->_isDefaultFocusEntered;
    }
    if (!CanTakeFocus(screen, TargetId, Window))
    {
        TargetId = windowId;
        IsEntered = false;
    }
    Widget* Target = NULL;
    if (!CanTakeFocus(screen, TargetId, Window) || !UIScreen_TryGetWidgetById(screen, TargetId, &Target))
    {
        return Error_CreateSuccess();
    }
    Error Result = UIScreen_SetFocusedWidget(screen, Target);
    // Entering gives an element the keys: only on a screen that has them (a world screen not in use only focuses), as a press does.
    if ((Result.Code != ErrorCode_Success) || !IsEntered || !screen->_hasKeyboardFocus || (UIScreen_GetFocusedWidgetId(screen) != TargetId)
        || !UIScreen_TryGetWidgetById(screen, TargetId, &Target) || !Widget_NeedsEntering(Target))
    {
        return Result; // A focus hook that moved the focus on wins.
    }
    return UIScreen_EnterWidget(screen, TargetId);
}

/* Restores a minimized window the user asked back (its first button, a double click on its title bar). The press gave the focus to the
 * window itself, its content hidden then; now that it shows, the focus goes on to what had it inside. */
static Error RestoreFromMinimized(UIScreen* screen, uint64_t windowId)
{
    Error Result = UIWindow_Restore(screen, windowId);
    return (Result.Code == ErrorCode_Success) ? ActivateWindow(screen, windowId) : Result;
}


// Static functions: the states.
/* Puts a root window in the layer its state calls for: Maximized while maximized and not minimized, Floating otherwise. */
static Error ApplyLayer(UIScreen* screen, uint64_t windowId)
{
    Widget* Window = NULL;
    UIWindowCapability* Self = FindWindow(screen, windowId, &Window);
    if ((Self == NULL) || !Widget_IsScreenRoot(Window))
    {
        return Error_CreateSuccess();
    }
    UIScreenLayer Wanted = (Self->_isMaximized && !Self->_isMinimized) ? UIScreenLayer_Maximized : UIScreenLayer_Floating;
    return (UIScreen_GetRootLayer(screen, Window) != Wanted) ? UIScreen_SetRootLayer(screen, Window, Wanted) : Error_CreateSuccess();
}

static Error SetPartShown(UIScreen* screen, uint64_t partId, bool isShown)
{
    Widget* Part = NULL;
    return UIScreen_TryGetWidgetById(screen, partId, &Part) ? Widget_SetIsRendered(Part, isShown) : Error_CreateSuccess();
}

/* Shows the parts the window's state calls for, each found again by id (hiding one can run hooks): the content while it is not
 * minimized, the frame and the title bar unless it is docked, the caption buttons it offers with the letters of its state, the edges while
 * it can be resized. */
static Error ApplyParts(UIScreen* screen, uint64_t windowId)
{
    Widget* Window = NULL;
    UIWindowCapability* Self = FindWindow(screen, windowId, &Window);
    if (Self == NULL)
    {
        return Error_CreateSuccess();
    }
    // Copied first: a hook run by hiding a part may change the window, or take it away.
    UIWindowCapability State = *Self;
    bool IsRoot = Widget_IsScreenRoot(Window);
    bool IsDocked = State._dock == UIWindowDock_Docked;
    bool AreEdgesShown = State._isResizable && !State._isMinimized && !State._isMaximized && IsRoot && !IsDocked;
    Error Result = SetPartShown(screen, State._areaId, !State._isMinimized);
    if (Result.Code == ErrorCode_Success)
    {
        Result = SetPartShown(screen, State._frameId, !IsDocked);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = SetPartShown(screen, State._titleBarId, !IsDocked);
    }
    UIKit* Kit = UIKit_FromScreen(screen);
    for (int32_t Button = 0; (Button < (int32_t)UIWindowButton_Count) && (Result.Code == ErrorCode_Success); Button++)
    {
        Result = SetPartShown(screen, State._buttonIds[Button], IsButtonShown(&State, (UIWindowButton)Button, IsRoot));
        Widget* ButtonWidget = NULL;
        if ((Result.Code != ErrorCode_Success) || !UIScreen_TryGetWidgetById(screen, State._buttonIds[Button], &ButtonWidget))
        {
            continue;
        }
        const UIButtonCapability* Capability = Widget_GetCapability(ButtonWidget, UIKit_GetCapabilityId(Kit, UIElementCapability_Button));
        UIElementIcon Wanted = UIElementIcon_FromLetter(GetButtonLetter(&State, (UIWindowButton)Button));
        if ((Capability != NULL) && !UIElementIcon_Equals(&Capability->_icon, &Wanted))
        {
            Result = UIButton_SetIcon(screen, State._buttonIds[Button], &Wanted);
        }
    }
    for (int32_t Edge = 0; (Edge < (int32_t)UIWindowEdge_Count) && (Result.Code == ErrorCode_Success); Edge++)
    {
        Result = SetPartShown(screen, State._edgeIds[Edge], AreEdgesShown);
    }
    return Result;
}

/* Applies a state change at once: the layer, then the parts. */
static Error ApplyState(UIScreen* screen, uint64_t windowId)
{
    Error Result = ApplyLayer(screen, windowId);
    return (Result.Code == ErrorCode_Success) ? ApplyParts(screen, windowId) : Result;
}

/* Every other root window of the screen that shows maximized is so no more: one is maximized at a time. A minimized one keeps that it was
 * maximized (restoring it restores the others in turn). Their layers follow at once. */
static Error RestoreOtherMaximized(UIScreen* screen, uint64_t windowId)
{
    UIKit* Kit = UIKit_FromScreen(screen);
    uint64_t Capability = UIKit_GetCapabilityId(Kit, UIElementCapability_Window);
    for (size_t Index = 0U; Index < UIScreen_GetRootCount(screen); Index++)
    {
        Widget* Root = UIScreen_GetRootAt(screen, Index);
        UIWindowCapability* Other = (Root != NULL) ? Widget_GetCapability(Root, Capability) : NULL;
        if ((Other == NULL) || (Widget_GetId(Root) == windowId) || !Other->_isMaximized || Other->_isMinimized)
        {
            continue;
        }
        Other->_isMaximized = false;
        uint64_t OtherId = Widget_GetId(Root);
        Error Result = ApplyState(screen, OtherId);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}


// Static functions: placing it.
/* Where a root window shows: the workspace while maximized; its title bar at its floating box's place while minimized; else its floating
 * box grown to its least size; kept reachable on the workspace. Sets its placement when it changed. */
static Error PlaceFloating(Widget* widget, UIWindowCapability* self)
{
    UIScreen* Screen = Widget_GetScreen(widget);
    Rectangle Workspace = UIScreen_GetWorkspaceBox(Screen);
    float Own = GetOwnScale(widget);
    const UIWindowMetrics* Metrics = &self->_metrics;
    Rectangle Shown = self->_floatingBox;
    if (self->_isMaximized && !self->_isMinimized)
    {
        Shown = Workspace;
    }
    else if (self->_isMinimized)
    {
        Shown.width = Math_MaxFloat(Metrics->MinimizedWidth, GetTitleBarNeeds(self, true)) * Own;
        Shown.height = Metrics->TitleHeight * Own;
    }
    else
    {
        // Its least height comes measured at the width it gets: the wanted one, or its least width when that is wider.
        Vector2 Least = { .x = 0.0f, .y = 0.0f };
        Error Result = MeasureLeast(widget, Shown.width, &Least);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        Shown.width = Math_MaxFloat(Shown.width, Least.x);
        Shown.height = Math_MaxFloat(Shown.height, Least.y);
    }
    if (!(self->_isMaximized && !self->_isMinimized))
    {
        Shown = UIWindow_KeepReachable(Shown, Workspace, Metrics->TitleHeight * Own, GetButtonsWidth(self, true) * Own,
            UI_WINDOW_REACHABLE_WIDTH * Own);
    }
    self->_shownBox = Shown;
    UIPlacement Placement = UIPlacement_FromTopLeft(Shown.x, Shown.y, Shown.width, Shown.height);
    UIPlacement Current = Widget_GetPlacement(widget);
    if (Widget_HasPlacement(widget) && Memory_IsEqual(&Current, &Placement, sizeof(Placement)))
    {
        return Error_CreateSuccess();
    }
    return Widget_SetPlacement(widget, &Placement);
}


// Static functions: moving and resizing.
/* Tells a window's move observer what its move by the title bar did. The observer may take the window away. */
static Error NotifyMove(UIScreen* screen, uint64_t windowId, UIWindowMoveStage stage, Vector2 screenPosition)
{
    UIWindowCapability* Self = FindWindow(screen, windowId, NULL);
    if ((Self == NULL) || (Self->_moveObserver == NULL))
    {
        return Error_CreateSuccess();
    }
    UserData Data = Self->_moveObserverData; // The observer may take the window away.
    UIWindowMoveObserver Observer = Self->_moveObserver;
    return Observer(screen, windowId, stage, screenPosition, &Data);
}

/* A press on the title bar or an edge begins moving or resizing a root window of its own that allows it, from a box. Whether it began. */
static bool BeginGrabFrom(UIScreen* screen, uint64_t windowId, UIWindowGrab grab, UIWindowEdge edge, GameKey button, Vector2 screenPosition,
    bool isFromFloatingBox)
{
    Widget* Window = NULL;
    UIWindowCapability* Self = FindWindow(screen, windowId, &Window);
    if ((Self == NULL) || !Widget_IsScreenRoot(Window) || (Self->_isMaximized && !Self->_isMinimized) || (Self->_dock == UIWindowDock_Docked))
    {
        return false;
    }
    if ((grab == UIWindowGrab_Resize) && (!Self->_isResizable || Self->_isMinimized))
    {
        return false;
    }
    Self->_grab = grab;
    Self->_grabEdge = edge;
    Self->_grabStart = ToScreenUnits(screen, screenPosition);
    Self->_grabBox = isFromFloatingBox ? Self->_floatingBox : Self->_shownBox;
    Self->_grabFloatingBox = Self->_floatingBox;
    Self->_grabButton = button;
    return true;
}

static bool BeginGrab(UIScreen* screen, uint64_t windowId, UIWindowGrab grab, UIWindowEdge edge, const WidgetMouseInputArgs* args)
{
    return BeginGrabFrom(screen, windowId, grab, edge, args->Button, args->ScreenPosition, false);
}

/* The pointer holding the window went somewhere: its floating box follows, reachable on the workspace. Moving keeps the floating size (a
 * minimized window's too); resizing keeps it at least the window's least size, the height measured at the new width. Each step starts
 * from where the grab began, so a second held button's moves (the same pointer) change nothing more. */
static Error FollowGrab(UIScreen* screen, uint64_t windowId, const WidgetMouseInputArgs* args)
{
    Widget* Window = NULL;
    UIWindowCapability* Self = FindWindow(screen, windowId, &Window);
    if ((Self == NULL) || (Self->_grab == UIWindowGrab_None))
    {
        return Error_CreateSuccess();
    }
    Vector2 Now = ToScreenUnits(screen, args->ScreenPosition);
    Vector2 Delta = { .x = Now.x - Self->_grabStart.x, .y = Now.y - Self->_grabStart.y };
    if (!IsFiniteFloat(Delta.x) || !IsFiniteFloat(Delta.y))
    {
        return Error_CreateSuccess();
    }
    Rectangle Workspace = UIScreen_GetWorkspaceBox(screen);
    float Own = GetOwnScale(Window);
    float TitleHeight = Self->_metrics.TitleHeight * Own;
    float ButtonsWidth = GetButtonsWidth(Self, true) * Own;
    float Reachable = UI_WINDOW_REACHABLE_WIDTH * Own;
    if (Self->_grab == UIWindowGrab_Move)
    {
        Rectangle Moved = Self->_grabBox;
        Moved.x += Delta.x;
        Moved.y += Delta.y;
        Moved = UIWindow_KeepReachable(Moved, Workspace, TitleHeight, ButtonsWidth, Reachable);
        Self->_floatingBox.x = Moved.x;
        Self->_floatingBox.y = Moved.y;
        return NotifyMove(screen, windowId, UIWindowMoveStage_Moved, args->ScreenPosition);
    }
    Vector2 Least = { .x = 0.0f, .y = 0.0f };
    Error Result = MeasureLeast(Window, Self->_grabBox.width, &Least);
    Rectangle Resized = UIWindow_ResizeBox(Self->_grabBox, Self->_grabEdge, Delta, Least, Workspace, TitleHeight);
    if ((Result.Code == ErrorCode_Success) && (Resized.width != Self->_grabBox.width))
    {
        Result = MeasureLeast(Window, Resized.width, &Least); // What fits down needs its height at the new width.
        Resized = UIWindow_ResizeBox(Self->_grabBox, Self->_grabEdge, Delta, Least, Workspace, TitleHeight);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Self = FindWindow(screen, windowId, &Window); // Measuring ran the content's hooks.
    if (Self != NULL)
    {
        Self->_floatingBox = UIWindow_KeepReachable(Resized, Workspace, TitleHeight, ButtonsWidth, Reachable);
    }
    return Error_CreateSuccess();
}

/* Ends the grab of a button's press. Returns what it was doing. */
static UIWindowGrab EndGrab(UIScreen* screen, uint64_t windowId, GameKey button)
{
    UIWindowCapability* Self = FindWindow(screen, windowId, NULL);
    if ((Self == NULL) || (Self->_grabButton != button))
    {
        return UIWindowGrab_None;
    }
    UIWindowGrab Ended = Self->_grab;
    Self->_grab = UIWindowGrab_None;
    return Ended;
}

/* Ends a move or a resize for a state that allows none (minimized, maximized, docked); the press goes on doing nothing. Whether it was a
 * move, which its observer hears lost once the state applies (AfterStateStoppedGrab). */
static bool StopGrabForState(UIWindowCapability* self)
{
    bool WasMoving = self->_grab == UIWindowGrab_Move;
    self->_grab = UIWindowGrab_None;
    return WasMoving;
}

static Error AfterStateStoppedGrab(UIScreen* screen, uint64_t windowId, bool wasMoving, Error result)
{
    if ((result.Code != ErrorCode_Success) || !wasMoving)
    {
        return result;
    }
    return NotifyMove(screen, windowId, UIWindowMoveStage_Lost, UIScreen_GetPointerPosition(screen));
}

/* Escape during a move or a resize puts the floating box back where the drag began; the press goes on doing nothing. */
static Error CancelGrab(UIScreen* screen, uint64_t windowId)
{
    UIWindowCapability* Self = FindWindow(screen, windowId, NULL);
    if ((Self == NULL) || (Self->_grab == UIWindowGrab_None))
    {
        return Error_CreateSuccess();
    }
    UIWindowGrab Cancelled = Self->_grab;
    Self->_grab = UIWindowGrab_None;
    Self->_floatingBox = Self->_grabFloatingBox;
    return (Cancelled == UIWindowGrab_Move) ? NotifyMove(screen, windowId, UIWindowMoveStage_Cancelled, UIScreen_GetPointerPosition(screen))
        : Error_CreateSuccess();
}

/* Maximizes a window that is not, restores one that is; a minimized one is restored, the focus going back inside. Only a root of its
 * screen fills the workspace: one placed by its parent is left as it is. */
static Error ToggleMaximized(UIScreen* screen, uint64_t windowId)
{
    Widget* Window = NULL;
    UIWindowCapability* Self = FindWindow(screen, windowId, &Window);
    if (Self == NULL)
    {
        return Error_CreateSuccess();
    }
    if (Self->_isMinimized)
    {
        return RestoreFromMinimized(screen, windowId);
    }
    if (Self->_isMaximized)
    {
        return UIWindow_Restore(screen, windowId);
    }
    return (Self->_canMaximize && Widget_IsScreenRoot(Window)) ? UIWindow_Maximize(screen, windowId) : Error_CreateSuccess();
}

/* Whether a window's held title bar press is a double click's second, taking the mark off. */
static bool TakeToggleOnRelease(UIScreen* screen, uint64_t windowId)
{
    UIWindowCapability* Self = FindWindow(screen, windowId, NULL);
    if (Self == NULL)
    {
        return false;
    }
    bool IsMarked = Self->_isToggleOnRelease;
    Self->_isToggleOnRelease = false;
    return IsMarked;
}


// Static functions: the frame, the title bar and the edges, the kit's own widget types.
static Error Frame_Construct(void* widget, void* args)
{
    UIWindowFrame* Frame = widget;
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIWindow: a frame needs its window's id.");
    }
    Frame->WindowId = *(const uint64_t*)args;
    return Widget_SetKeepsFocusOnPress(&Frame->Base, true);
}

/* A press on the border gives the window the focus as one on the title bar does, acting on nothing inside. */
static Error Frame_OnMouseInput(void* self, const WidgetMouseInputArgs* args)
{
    UIWindowFrame* Frame = self;
    return (args->Type == UIMouseInputType_ButtonPress) ? ActivateWindow(Widget_GetScreen(&Frame->Base), Frame->WindowId) : Error_CreateSuccess();
}

static Error TitleBar_Construct(void* widget, void* args)
{
    UIWindowTitleBar* Bar = widget;
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIWindow: a title bar needs its window's id.");
    }
    Bar->WindowId = *(const uint64_t*)args;
    return Widget_SetKeepsFocusOnPress(&Bar->Base, true);
}

/* A press gives the window the focus (acting on nothing inside) and, with the left button, moves it. A second press in a row maximizes or
 * restores it when it is let go: its release lands on the title bar still, never on what the window shows there afterwards. */
static Error TitleBar_OnMouseInput(void* self, const WidgetMouseInputArgs* args)
{
    UIWindowTitleBar* Bar = self;
    UIScreen* Screen = Widget_GetScreen(&Bar->Base);
    uint64_t WindowId = Bar->WindowId;
    switch (args->Type)
    {
        case UIMouseInputType_ButtonPress:
        {
            Error Result = ActivateWindow(Screen, WindowId);
            if ((Result.Code != ErrorCode_Success) || (args->Button != GameKey_MouseLeft))
            {
                return Result;
            }
            if (args->ClickCount == 2U)
            {
                UIWindowCapability* Window = FindWindow(Screen, WindowId, NULL);
                if (Window != NULL)
                {
                    Window->_isToggleOnRelease = true;
                }
                return Error_CreateSuccess();
            }
            bool IsMoving = BeginGrab(Screen, WindowId, UIWindowGrab_Move, UIWindowEdge_Count, args);
            return IsMoving ? NotifyMove(Screen, WindowId, UIWindowMoveStage_Began, args->ScreenPosition) : Error_CreateSuccess();
        }
        case UIMouseInputType_Move:
            return FollowGrab(Screen, WindowId, args);
        case UIMouseInputType_ButtonRelease:
        {
            if (EndGrab(Screen, WindowId, args->Button) == UIWindowGrab_Move)
            {
                Error Result = NotifyMove(Screen, WindowId, UIWindowMoveStage_Released, args->ScreenPosition);
                if (Result.Code != ErrorCode_Success)
                {
                    return Result;
                }
            }
            return ((args->Button == GameKey_MouseLeft) && TakeToggleOnRelease(Screen, WindowId)) ? ToggleMaximized(Screen, WindowId)
                : Error_CreateSuccess();
        }
        case UIMouseInputType_ReleaseOver:
            return Error_CreateSuccess();
    }
    return Error_CreateSuccess();
}

static Error TitleBar_OnPressLost(void* self, GameKey button)
{
    UIWindowTitleBar* Bar = self;
    UIScreen* Screen = Widget_GetScreen(&Bar->Base);
    uint64_t WindowId = Bar->WindowId;
    if (button == GameKey_MouseLeft)
    {
        (void)TakeToggleOnRelease(Screen, WindowId); // A double click whose press was lost does nothing.
    }
    bool WasMoving = EndGrab(Screen, WindowId, button) == UIWindowGrab_Move;
    return WasMoving ? NotifyMove(Screen, WindowId, UIWindowMoveStage_Lost, UIScreen_GetPointerPosition(Screen)) : Error_CreateSuccess();
}

static Error Edge_Construct(void* widget, void* args)
{
    UIWindowEdgeWidget* Edge = widget;
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIWindow: an edge needs its window's id and side.");
    }
    const UIWindowEdgeArgs* Arguments = args;
    Edge->WindowId = Arguments->WindowId;
    Edge->Edge = Arguments->Edge;
    Error Result = Widget_SetCursorShape(&Edge->Base, GetEdgeShape(Arguments->Edge));
    return (Result.Code == ErrorCode_Success) ? Widget_SetKeepsFocusOnPress(&Edge->Base, true) : Result;
}

/* A press gives the window the focus (acting on nothing inside) and, with the left button, resizes it by this edge. */
static Error Edge_OnMouseInput(void* self, const WidgetMouseInputArgs* args)
{
    UIWindowEdgeWidget* Edge = self;
    UIScreen* Screen = Widget_GetScreen(&Edge->Base);
    uint64_t WindowId = Edge->WindowId;
    UIWindowEdge Side = Edge->Edge;
    switch (args->Type)
    {
        case UIMouseInputType_ButtonPress:
        {
            Error Result = ActivateWindow(Screen, WindowId);
            if ((Result.Code == ErrorCode_Success) && (args->Button == GameKey_MouseLeft))
            {
                (void)BeginGrab(Screen, WindowId, UIWindowGrab_Resize, Side, args);
            }
            return Result;
        }
        case UIMouseInputType_Move:
            return FollowGrab(Screen, WindowId, args);
        case UIMouseInputType_ButtonRelease:
            (void)EndGrab(Screen, WindowId, args->Button);
            return Error_CreateSuccess();
        case UIMouseInputType_ReleaseOver:
            return Error_CreateSuccess();
    }
    return Error_CreateSuccess();
}

static Error Edge_OnPressLost(void* self, GameKey button)
{
    UIWindowEdgeWidget* Edge = self;
    (void)EndGrab(Widget_GetScreen(&Edge->Base), Edge->WindowId, button);
    return Error_CreateSuccess();
}


// Static functions: the caption buttons.
/* A caption button's events: a press gives the window the focus (acting on nothing inside, as the title bar does), an activation does
 * what the button is for. */
static Error OnCaptionEvent(UIScreen* screen, const UIEvent* event, const UserData* userData)
{
    uint64_t WindowId = ReadIdData(userData);
    UIWindowCapability* Self = FindWindow(screen, WindowId, NULL);
    if (Self == NULL)
    {
        return Error_CreateSuccess(); // Gone with its buttons.
    }
    if (event->WidgetEventId == (int32_t)UIElementEvent_Pressed)
    {
        return ActivateWindow(screen, WindowId);
    }
    if (event->WidgetEventId != (int32_t)UIElementEvent_Activated)
    {
        return Error_CreateSuccess();
    }
    if (event->WidgetId == Self->_buttonIds[UIWindowButton_Minimize])
    {
        return Self->_isMinimized ? RestoreFromMinimized(screen, WindowId) : UIWindow_Minimize(screen, WindowId);
    }
    if (event->WidgetId == Self->_buttonIds[UIWindowButton_Maximize])
    {
        return ToggleMaximized(screen, WindowId);
    }
    if (event->WidgetId == Self->_buttonIds[UIWindowButton_Close])
    {
        return UIWindow_Close(screen, WindowId, NULL);
    }
    return Error_CreateSuccess();
}


// Static functions: the behaviour.
static void* ResolveWindowCapability(void* widget)
{
    return GetCapability(widget);
}

static Error Behaviour_Construct(void* widget, void* args)
{
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIWindow: the arguments are required.");
    }
    const UIWindowOptions* Options = ((const UIWindowArgs*)args)->Options;
    if (!UIWindowOptions_IsValid(Options))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "UIWindow: the title must be valid UTF-8, the icon valid, the size above 0, the least size >= 0, every number finite and the fits known.");
    }
    UIElementOptions Element = Options->Element;
    Element.HasPlacement = false; // A window is placed by its own box.
    Error Result = UIElement_ApplyOptions(widget, &Element);
    UIWindowCapability* Self = GetCapability(widget);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UITextContent_Construct(&Self->_title, Options->Title);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    UIScreen* Screen = Widget_GetScreen(widget);
    Rectangle Workspace = UIScreen_GetWorkspaceBox(Screen);
    Self->_icon = Options->Icon;
    Self->_floatingBox = (Rectangle){ .x = Options->Position.x, .y = Options->Position.y, .width = Options->Size.x, .height = Options->Size.y };
    if (!Options->HasPosition)
    {
        Self->_floatingBox.x = Math_FloorFloat(Workspace.x + ((Workspace.width - Options->Size.x) * 0.5f));
        Self->_floatingBox.y = Math_FloorFloat(Workspace.y + ((Workspace.height - Options->Size.y) * 0.5f));
    }
    Self->_shownBox = Self->_floatingBox;
    Self->_minimumSize = Options->MinimumSize;
    Self->_fitAcross = Options->FitAcross;
    Self->_fitDown = Options->FitDown;
    Self->_isResizable = Options->IsResizable;
    Self->_canMinimize = Options->CanMinimize;
    Self->_canMaximize = Options->CanMaximize;
    Self->_canClose = Options->CanClose;
    Self->_isMinimized = false;
    Self->_isMaximized = false;
    Self->_isActive = false;
    Self->_lastFocusId = WIDGET_ID_INVALID;
    Self->_wasLastFocusEntered = false;
    Self->_defaultFocusId = WIDGET_ID_INVALID;
    Self->_isDefaultFocusEntered = false;
    Self->_closeQuery = Options->CloseQuery;
    Self->_closeData = Options->CloseData;
    Self->_frameId = WIDGET_ID_INVALID;
    Self->_titleBarId = WIDGET_ID_INVALID;
    Self->_areaId = WIDGET_ID_INVALID;
    Self->_contentId = WIDGET_ID_INVALID;
    for (size_t Index = 0U; Index < (size_t)UIWindowButton_Count; Index++)
    {
        Self->_buttonIds[Index] = WIDGET_ID_INVALID;
    }
    for (size_t Index = 0U; Index < (size_t)UIWindowEdge_Count; Index++)
    {
        Self->_edgeIds[Index] = WIDGET_ID_INVALID;
    }
    Self->_buttonsStart = 0.0f;
    Self->_grab = UIWindowGrab_None;
    Self->_grabEdge = UIWindowEdge_Count;
    Self->_grabStart = (Vector2){ .x = 0.0f, .y = 0.0f };
    Self->_grabBox = Self->_floatingBox;
    Self->_grabFloatingBox = Self->_floatingBox;
    Self->_grabButton = GameKey_Unknown;
    Self->_dock = UIWindowDock_None;
    Self->_moveObserver = NULL;
    Self->_moveObserverData = UserData_CreateEmpty();
    Self->_isToggleOnRelease = false;
    Self->_types = NULL;
    Self->_typeIndex = 0U;
    Self->_metrics = (UIWindowMetrics){ .TitleHeight = 24.0f, .TitlePadding = 6.0f, .IconSpacing = 6.0f, .IconWidth = 0.0f,
        .ButtonSize = { .x = 22.0f, .y = 18.0f }, .ButtonSpacing = 2.0f, .Border = 3.0f, .ResizeMargin = 5.0f,
        .MinSize = { .x = 160.0f, .y = 80.0f }, .MinimizedWidth = 200.0f, .ContentPadding = { .x = 8.0f, .y = 8.0f }, .ContentSpacing = 6.0f };
    UIPlacement Placement = UIPlacement_FromTopLeft(Self->_floatingBox.x, Self->_floatingBox.y, Self->_floatingBox.width,
        Self->_floatingBox.height);
    Result = Widget_SetPlacement(widget, &Placement);
    if (Result.Code != ErrorCode_Success)
    {
        UITextContent_Deconstruct(&Self->_title);
    }
    return Result;
}

static Error Behaviour_Deconstruct(void* widget)
{
    UITextContent_Deconstruct(&GetCapability(widget)->_title);
    return Error_CreateSuccess();
}

/* Follows the focus, places a root window (a docked one is placed by its parent) and puts its layer and parts in step with its state. */
static Error Behaviour_Update(void* widget, ProgramTime time)
{
    (void)time;
    UIWindowCapability* Self = GetCapability(widget);
    UIScreen* Screen = Widget_GetScreen(widget);
    uint64_t Id = Widget_GetId(widget);
    TrackFocus(widget, Self);
    if (Widget_IsScreenRoot(widget))
    {
        Error Result = PlaceFloating(widget, Self);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    else
    {
        // Placed by its parent: what shows is what the last layout gave it.
        Rectangle Pixels = UIScreen_GetWidgetPixelBox(Screen, widget);
        float Scale = UIScreen_GetScale(Screen);
        if (Scale > 0.0f)
        {
            Self->_shownBox = (Rectangle){ .x = Pixels.x / Scale, .y = Pixels.y / Scale, .width = Pixels.width / Scale, .height = Pixels.height / Scale };
        }
    }
    return ApplyState(Screen, Id);
}

/* The frame under everything, the title bar across the top with the caption buttons at its right, the content inside the border under
 * it, the edges along the frame; docked, the content fills the box. */
static Error Behaviour_Layout(void* widget, const UILayoutContext* context)
{
    (void)context;
    UIWindowCapability* Self = GetCapability(widget);
    UIScreen* Screen = Widget_GetScreen(widget);
    Rectangle Pixels = UIScreen_GetWidgetPixelBox(Screen, widget);
    float Scale = Widget_GetEffectiveScale(widget);
    float Width = Pixels.width / Scale;
    float Height = Pixels.height / Scale;
    bool IsRoot = Widget_IsScreenRoot(widget);
    const UIWindowMetrics* Metrics = &Self->_metrics;
    float TitleHeight = GetTitleHeight(Self);
    float Border = GetBorder(Self);
    Error Result = PlaceAt(Screen, Self->_frameId, (Rectangle){ .x = 0.0f, .y = 0.0f, .width = Width, .height = Height });
    if (Result.Code == ErrorCode_Success)
    {
        Result = PlaceAt(Screen, Self->_titleBarId, (Rectangle){ .x = 0.0f, .y = 0.0f, .width = Width, .height = TitleHeight });
    }
    float X = Width - Metrics->TitlePadding;
    float ButtonY = Math_FloorFloat((Metrics->TitleHeight - Metrics->ButtonSize.y) * 0.5f);
    bool HasOne = false;
    for (int32_t Button = (int32_t)UIWindowButton_Count - 1; (Button >= 0) && (Result.Code == ErrorCode_Success); Button--)
    {
        if (!IsButtonShown(Self, (UIWindowButton)Button, IsRoot))
        {
            continue;
        }
        X -= Metrics->ButtonSize.x + (HasOne ? Metrics->ButtonSpacing : 0.0f);
        HasOne = true;
        Result = PlaceAt(Screen, Self->_buttonIds[Button], (Rectangle){ .x = X, .y = ButtonY, .width = Metrics->ButtonSize.x,
            .height = Metrics->ButtonSize.y });
    }
    Self->_buttonsStart = X;
    if (Result.Code == ErrorCode_Success)
    {
        Result = PlaceAt(Screen, Self->_areaId, (Rectangle){ .x = Border, .y = TitleHeight, .width = Width - (2.0f * Border),
            .height = Height - TitleHeight - Border });
    }
    for (int32_t Edge = 0; (Edge < (int32_t)UIWindowEdge_Count) && (Result.Code == ErrorCode_Success); Edge++)
    {
        Result = PlaceAt(Screen, Self->_edgeIds[Edge], GetEdgeBand((UIWindowEdge)Edge, Width, Height, Metrics->ResizeMargin));
    }
    return Result;
}

/* Escape during a move or a resize (the window hears it as the focused widget or as an ancestor of it) puts it back where the drag began. */
static Error Behaviour_OnKeyboardInput(void* widget, const WidgetKeyboardInputArgs* args, bool* outIsHandled)
{
    UIWindowCapability* Self = GetCapability(widget);
    UIScreen* Screen = Widget_GetScreen(widget);
    if ((Self->_grab == UIWindowGrab_None) || (args->Type != UIKeyboardInputType_KeyPress) || args->IsHoverDelivery
        || !UIElement_IsActionPress(Screen, GameAction_UIBack, args->Key))
    {
        return Error_CreateSuccess();
    }
    *outIsHandled = true;
    return CancelGrab(Screen, Widget_GetId(widget));
}

/* What last had the focus inside the window is where a press on its title bar gives it back. Entered or not, the window's next update
 * tells; until then the default target counts as entered when it asked to be, so one focused, entered and left within one tick (a layout being
 * built) is given back as it asked. */
static Error Behaviour_OnDescendantFocus(void* widget, uint64_t widgetId)
{
    UIWindowCapability* Self = GetCapability(widget);
    if (Self->_lastFocusId != widgetId)
    {
        Self->_lastFocusId = widgetId;
        Self->_wasLastFocusEntered = (widgetId == Self->_defaultFocusId) && Self->_isDefaultFocusEntered;
    }
    return Error_CreateSuccess();
}


// Fields.
static const UIKindCapability WindowCapabilities[] =
{
    { .Capability = UIElementCapability_Window, .Resolve = ResolveWindowCapability }
};

static const UIKindBehaviour WindowBehaviour =
{
    .Kind = UIElementKind_Window,
    .Capabilities = WindowCapabilities,
    .CapabilityCount = sizeof(WindowCapabilities) / sizeof(WindowCapabilities[0]),
    .BaseSize = sizeof(UIWindowLookBase),
    .IsFocusable = true,
    .NeedsEntering = false,
    .EntersItself = false,
    .Construct = Behaviour_Construct,
    .Deconstruct = Behaviour_Deconstruct,
    .Update = Behaviour_Update,
    .Layout = Behaviour_Layout,
    .OnKeyboardInput = Behaviour_OnKeyboardInput,
    .OnDescendantFocus = Behaviour_OnDescendantFocus
};

static const WidgetVTable FrameVTable =
{
    .OnMouseInput = Frame_OnMouseInput
};

static const WidgetVTable TitleBarVTable =
{
    .OnMouseInput = TitleBar_OnMouseInput,
    .OnPressLost = TitleBar_OnPressLost
};

static const WidgetVTable EdgeVTable =
{
    .OnMouseInput = Edge_OnMouseInput,
    .OnPressLost = Edge_OnPressLost
};


// Internal functions (declared in UIKitInternal.h).
const UIKindBehaviour* UIWindow_GetKindBehaviour(void)
{
    return &WindowBehaviour;
}


// Public functions: options.
bool UIWindowOptions_IsValid(const UIWindowOptions* options)
{
    bool IsTitleValid = (options->Title == NULL) || StringUTF8_IsEncodingValid(options->Title);
    bool IsIconValid = (options->Icon.Kind == UIElementIconKind_None) || UIElementIcon_IsValid(&options->Icon);
    bool IsPositionValid = !options->HasPosition || (IsFiniteFloat(options->Position.x) && IsFiniteFloat(options->Position.y)
        && (Math_AbsFloat(options->Position.x) <= UI_LAYOUT_MAX_LENGTH) && (Math_AbsFloat(options->Position.y) <= UI_LAYOUT_MAX_LENGTH));
    bool IsSizeValid = UILayoutLength_IsValid(options->Size.x) && UILayoutLength_IsValid(options->Size.y) && (options->Size.x > 0.0f)
        && (options->Size.y > 0.0f);
    bool IsMinimumValid = UILayoutLength_IsValid(options->MinimumSize.x) && UILayoutLength_IsValid(options->MinimumSize.y);
    bool AreFitsValid = ((options->FitAcross == UIWindowFit_Fit) || (options->FitAcross == UIWindowFit_Scroll))
        && ((options->FitDown == UIWindowFit_Fit) || (options->FitDown == UIWindowFit_Scroll));
    return IsTitleValid && IsIconValid && IsPositionValid && IsSizeValid && IsMinimumValid && AreFitsValid;
}


// Public functions: calling code.
Error UIWindow_GetContentId(UIScreen* screen, uint64_t windowId, uint64_t* outContentId)
{
    Widget* Target = NULL;
    UIWindowCapability* Self = NULL;
    Error Result = ResolveWindow(screen, windowId, &Target, &Self);
    if (Result.Code == ErrorCode_Success)
    {
        *outContentId = Self->_contentId;
    }
    return Result;
}

bool UIWindow_IsWindow(UIScreen* screen, uint64_t widgetId)
{
    return FindWindow(screen, widgetId, NULL) != NULL;
}

Error UIWindow_SetTitle(UIScreen* screen, uint64_t windowId, const unsigned char* title)
{
    Widget* Target = NULL;
    UIWindowCapability* Self = NULL;
    Error Result = ResolveWindow(screen, windowId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (title == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIWindow_SetTitle: the title must not be NULL.");
    }
    bool HasChanged = false;
    Result = UITextContent_Set(&Self->_title, title, &HasChanged);
    if ((Result.Code == ErrorCode_Success) && HasChanged)
    {
        Widget_InvalidateMeasure(Target);
    }
    return Result;
}

Error UIWindow_GetTitle(UIScreen* screen, uint64_t windowId, const unsigned char** outTitle)
{
    Widget* Target = NULL;
    UIWindowCapability* Self = NULL;
    Error Result = ResolveWindow(screen, windowId, &Target, &Self);
    if (Result.Code == ErrorCode_Success)
    {
        *outTitle = UITextContent_Get(&Self->_title);
    }
    return Result;
}

Error UIWindow_SetIcon(UIScreen* screen, uint64_t windowId, const UIElementIcon* icon)
{
    Widget* Target = NULL;
    UIWindowCapability* Self = NULL;
    Error Result = ResolveWindow(screen, windowId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((icon == NULL) || ((icon->Kind != UIElementIconKind_None) && !UIElementIcon_IsValid(icon)))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIWindow_SetIcon: the icon must be none or a valid one.");
    }
    Self->_icon = *icon;
    Widget_InvalidateMeasure(Target);
    return Error_CreateSuccess();
}

Error UIWindow_SetBox(UIScreen* screen, uint64_t windowId, Rectangle box)
{
    Widget* Target = NULL;
    UIWindowCapability* Self = NULL;
    Error Result = ResolveWindow(screen, windowId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    bool IsValid = IsFiniteFloat(box.x) && IsFiniteFloat(box.y) && (Math_AbsFloat(box.x) <= UI_LAYOUT_MAX_LENGTH)
        && (Math_AbsFloat(box.y) <= UI_LAYOUT_MAX_LENGTH) && UILayoutLength_IsValid(box.width) && UILayoutLength_IsValid(box.height)
        && (box.width > 0.0f) && (box.height > 0.0f);
    if (!IsValid)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIWindow_SetBox: the box must be finite, its size above 0.");
    }
    Self->_floatingBox = box;
    return Error_CreateSuccess();
}

Error UIWindow_GetBox(UIScreen* screen, uint64_t windowId, Rectangle* outBox)
{
    Widget* Target = NULL;
    UIWindowCapability* Self = NULL;
    Error Result = ResolveWindow(screen, windowId, &Target, &Self);
    if (Result.Code == ErrorCode_Success)
    {
        *outBox = Self->_floatingBox;
    }
    return Result;
}

Error UIWindow_GetShownBox(UIScreen* screen, uint64_t windowId, Rectangle* outBox)
{
    Widget* Target = NULL;
    UIWindowCapability* Self = NULL;
    Error Result = ResolveWindow(screen, windowId, &Target, &Self);
    if (Result.Code == ErrorCode_Success)
    {
        *outBox = Self->_shownBox;
    }
    return Result;
}

Error UIWindow_SetMinimumSize(UIScreen* screen, uint64_t windowId, Vector2 size)
{
    Widget* Target = NULL;
    UIWindowCapability* Self = NULL;
    Error Result = ResolveWindow(screen, windowId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!UILayoutLength_IsValid(size.x) || !UILayoutLength_IsValid(size.y))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIWindow_SetMinimumSize: each length must be within [0, UI_LAYOUT_MAX_LENGTH].");
    }
    Self->_minimumSize = size;
    Widget_InvalidateMeasure(Target);
    return Error_CreateSuccess();
}

Error UIWindow_SetFit(UIScreen* screen, uint64_t windowId, UIWindowFit across, UIWindowFit down)
{
    Widget* Target = NULL;
    UIWindowCapability* Self = NULL;
    Error Result = ResolveWindow(screen, windowId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    bool AreValid = ((across == UIWindowFit_Fit) || (across == UIWindowFit_Scroll)) && ((down == UIWindowFit_Fit) || (down == UIWindowFit_Scroll));
    if (!AreValid)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIWindow_SetFit: an unknown fit.");
    }
    Self->_fitAcross = across;
    Self->_fitDown = down;
    Widget_InvalidateMeasure(Target);
    return UIScrollable_SetModes(screen, Self->_areaId, (across == UIWindowFit_Fit) ? UIScrollMode_Disabled : UIScrollMode_Auto,
        (down == UIWindowFit_Fit) ? UIScrollMode_Disabled : UIScrollMode_Auto);
}

Error UIWindow_SetIsResizable(UIScreen* screen, uint64_t windowId, bool isResizable)
{
    Widget* Target = NULL;
    UIWindowCapability* Self = NULL;
    Error Result = ResolveWindow(screen, windowId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Self->_isResizable = isResizable;
    return ApplyParts(screen, windowId);
}

Error UIWindow_SetButtons(UIScreen* screen, uint64_t windowId, bool canMinimize, bool canMaximize, bool canClose)
{
    Widget* Target = NULL;
    UIWindowCapability* Self = NULL;
    Error Result = ResolveWindow(screen, windowId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Self->_canMinimize = canMinimize;
    Self->_canMaximize = canMaximize;
    Self->_canClose = canClose;
    Widget_InvalidateMeasure(Target); // The title bar needs another width.
    return ApplyParts(screen, windowId);
}

Error UIWindow_Minimize(UIScreen* screen, uint64_t windowId)
{
    Widget* Target = NULL;
    UIWindowCapability* Self = NULL;
    Error Result = ResolveWindow(screen, windowId, &Target, &Self);
    if ((Result.Code != ErrorCode_Success) || Self->_isMinimized)
    {
        return Result;
    }
    if (!Widget_IsScreenRoot(Target) || (Self->_dock != UIWindowDock_None))
    {
        return Error_Construct2(ErrorCode_InvalidOperation,
            "UIWindow_Minimize: only a window that is a root of its screen minimizes, and none from a dock.");
    }
    Self->_isMinimized = true;
    bool WasMoving = StopGrabForState(Self);
    Widget_InvalidateMeasure(Target); // Its title bar's buttons change.
    return AfterStateStoppedGrab(screen, windowId, WasMoving, ApplyState(screen, windowId));
}

Error UIWindow_Maximize(UIScreen* screen, uint64_t windowId)
{
    Widget* Target = NULL;
    UIWindowCapability* Self = NULL;
    Error Result = ResolveWindow(screen, windowId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!Widget_IsScreenRoot(Target))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWindow_Maximize: only a window that is a root of its screen fills the workspace.");
    }
    if (Self->_isMaximized && !Self->_isMinimized)
    {
        return Error_CreateSuccess();
    }
    Self->_isMaximized = true;
    Self->_isMinimized = false;
    bool WasMoving = StopGrabForState(Self);
    Widget_InvalidateMeasure(Target);
    Result = RestoreOtherMaximized(screen, windowId);
    return AfterStateStoppedGrab(screen, windowId, WasMoving, (Result.Code == ErrorCode_Success) ? ApplyState(screen, windowId) : Result);
}

Error UIWindow_Restore(UIScreen* screen, uint64_t windowId)
{
    Widget* Target = NULL;
    UIWindowCapability* Self = NULL;
    Error Result = ResolveWindow(screen, windowId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Widget_InvalidateMeasure(Target);
    if (Self->_isMinimized)
    {
        Self->_isMinimized = false;
        Result = Self->_isMaximized ? RestoreOtherMaximized(screen, windowId) : Error_CreateSuccess();
        return (Result.Code == ErrorCode_Success) ? ApplyState(screen, windowId) : Result;
    }
    if (!Self->_isMaximized)
    {
        return Error_CreateSuccess();
    }
    Self->_isMaximized = false;
    Result = ApplyState(screen, windowId);
    if ((Result.Code == ErrorCode_Success) && UIScreen_TryGetWidgetById(screen, windowId, &Target) && Widget_IsScreenRoot(Target))
    {
        Result = UIScreen_BringToTop(screen, Target); // Back among the floating windows, on top of them.
    }
    return Result;
}

Error UIWindow_GetState(UIScreen* screen, uint64_t windowId, bool* outIsMinimized, bool* outIsMaximized)
{
    Widget* Target = NULL;
    UIWindowCapability* Self = NULL;
    Error Result = ResolveWindow(screen, windowId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (outIsMinimized != NULL)
    {
        *outIsMinimized = Self->_isMinimized;
    }
    if (outIsMaximized != NULL)
    {
        *outIsMaximized = Self->_isMaximized;
    }
    return Error_CreateSuccess();
}

Error UIWindow_Close(UIScreen* screen, uint64_t windowId, bool* outIsClosed)
{
    if (outIsClosed != NULL)
    {
        *outIsClosed = false;
    }
    Widget* Target = NULL;
    UIWindowCapability* Self = NULL;
    Error Result = ResolveWindow(screen, windowId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (Self->_closeQuery != NULL)
    {
        UserData Data = Self->_closeData; // The query may take the window away.
        UIWindowCloseQuery Query = Self->_closeQuery;
        bool IsAllowed = Query(screen, windowId, &Data);
        if (!UIScreen_IsWidgetAlive(screen, windowId))
        {
            if (outIsClosed != NULL)
            {
                *outIsClosed = true; // The query closed it itself.
            }
            return Error_CreateSuccess();
        }
        if (!IsAllowed)
        {
            return Error_CreateSuccess();
        }
    }
    Result = UIWindow_CloseNow(screen, windowId);
    if ((Result.Code == ErrorCode_Success) && (outIsClosed != NULL))
    {
        *outIsClosed = true;
    }
    return Result;
}

Error UIWindow_CloseNow(UIScreen* screen, uint64_t windowId)
{
    Widget* Target = NULL;
    UIWindowCapability* Self = NULL;
    Error Result = ResolveWindow(screen, windowId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (Self->_types != NULL)
    {
        UIWindowTypes_RememberBox(Self->_types, Self->_typeIndex, Self->_floatingBox);
    }
    Result = UIElement_PostCue(Target, UIElementCue_Close); // Delivered even once it is gone.
    return (Result.Code == ErrorCode_Success) ? Widget_Destroy(Target) : Result;
}

Error UIWindow_SetCloseQuery(UIScreen* screen, uint64_t windowId, UIWindowCloseQuery query, const UserData* userData)
{
    Widget* Target = NULL;
    UIWindowCapability* Self = NULL;
    Error Result = ResolveWindow(screen, windowId, &Target, &Self);
    if (Result.Code == ErrorCode_Success)
    {
        Self->_closeQuery = query;
        Self->_closeData = (userData != NULL) ? *userData : UserData_CreateEmpty();
    }
    return Result;
}

Error UIWindow_Focus(UIScreen* screen, uint64_t windowId)
{
    Widget* Target = NULL;
    UIWindowCapability* Self = NULL;
    Error Result = ResolveWindow(screen, windowId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    // A window maximized over it would hide it: restored first (a minimized one keeps that it was maximized).
    if (!(Self->_isMaximized && !Self->_isMinimized))
    {
        Result = RestoreOtherMaximized(screen, windowId);
    }
    if ((Result.Code == ErrorCode_Success) && UIScreen_TryGetWidgetById(screen, windowId, &Target) && Widget_IsScreenRoot(Target))
    {
        Result = UIScreen_BringToTop(screen, Target);
    }
    return (Result.Code == ErrorCode_Success) ? ActivateWindow(screen, windowId) : Result;
}

Error UIWindow_SetDefaultFocus(UIScreen* screen, uint64_t windowId, uint64_t focusId, bool isEntered)
{
    Widget* Target = NULL;
    UIWindowCapability* Self = NULL;
    Error Result = ResolveWindow(screen, windowId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Widget* Focus = NULL;
    if ((focusId != WIDGET_ID_INVALID) && (!UIScreen_TryGetWidgetById(screen, focusId, &Focus) || !IsInside(Focus, Target)))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWindow_SetDefaultFocus: the focus target must lie inside the window.");
    }
    Self->_defaultFocusId = focusId;
    Self->_isDefaultFocusEntered = (focusId != WIDGET_ID_INVALID) && isEntered;
    return Error_CreateSuccess();
}

Error UIWindow_IsActive(UIScreen* screen, uint64_t windowId, bool* outIsActive)
{
    Widget* Target = NULL;
    UIWindowCapability* Self = NULL;
    Error Result = ResolveWindow(screen, windowId, &Target, &Self);
    if (Result.Code == ErrorCode_Success)
    {
        *outIsActive = Self->_isActive;
    }
    return Result;
}

Error UIWindow_GetButtonId(UIScreen* screen, uint64_t windowId, UIWindowButton button, uint64_t* outButtonId)
{
    Widget* Target = NULL;
    UIWindowCapability* Self = NULL;
    Error Result = ResolveWindow(screen, windowId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (((int32_t)button < 0) || ((int32_t)button >= (int32_t)UIWindowButton_Count))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIWindow_GetButtonId: an unknown button.");
    }
    *outButtonId = Self->_buttonIds[button];
    return Error_CreateSuccess();
}

Error UIWindow_GetType(UIScreen* screen, uint64_t windowId, UIWindowTypes** outTypes, uint32_t* outIndex)
{
    Widget* Target = NULL;
    UIWindowCapability* Self = NULL;
    Error Result = ResolveWindow(screen, windowId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    *outTypes = Self->_types;
    if (outIndex != NULL)
    {
        *outIndex = Self->_typeIndex;
    }
    return Error_CreateSuccess();
}

Error UIWindow_SetType(UIScreen* screen, uint64_t windowId, UIWindowTypes* types, uint32_t index)
{
    Widget* Target = NULL;
    UIWindowCapability* Self = NULL;
    Error Result = ResolveWindow(screen, windowId, &Target, &Self);
    if (Result.Code == ErrorCode_Success)
    {
        Self->_types = types;
        Self->_typeIndex = index;
    }
    return Result;
}

Error UIWindow_SetDock(UIScreen* screen, uint64_t windowId, UIWindowDock dock)
{
    Widget* Target = NULL;
    UIWindowCapability* Self = NULL;
    Error Result = ResolveWindow(screen, windowId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((dock != UIWindowDock_None) && (dock != UIWindowDock_Docked) && (dock != UIWindowDock_Maximized))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIWindow_SetDock: an unknown value.");
    }
    if (Self->_dock == dock)
    {
        return Error_CreateSuccess();
    }
    Self->_dock = dock;
    bool WasMoving = false;
    if (dock != UIWindowDock_None)
    {
        Self->_isMinimized = false; // Nothing from a dock minimizes.
        WasMoving = StopGrabForState(Self);
    }
    if (dock == UIWindowDock_Docked)
    {
        Self->_isMaximized = false;
    }
    Widget_InvalidateMeasure(Target); // Its title bar and border come or go.
    return AfterStateStoppedGrab(screen, windowId, WasMoving, ApplyState(screen, windowId));
}

Error UIWindow_GetDock(UIScreen* screen, uint64_t windowId, UIWindowDock* outDock)
{
    Widget* Target = NULL;
    UIWindowCapability* Self = NULL;
    Error Result = ResolveWindow(screen, windowId, &Target, &Self);
    if (Result.Code == ErrorCode_Success)
    {
        *outDock = Self->_dock;
    }
    return Result;
}

Error UIWindow_SetMoveObserver(UIScreen* screen, uint64_t windowId, UIWindowMoveObserver observer, const UserData* userData)
{
    Widget* Target = NULL;
    UIWindowCapability* Self = NULL;
    Error Result = ResolveWindow(screen, windowId, &Target, &Self);
    if (Result.Code == ErrorCode_Success)
    {
        Self->_moveObserver = observer;
        Self->_moveObserverData = (userData != NULL) ? *userData : UserData_CreateEmpty();
    }
    return Result;
}

Error UIWindow_StartMove(UIScreen* screen, uint64_t windowId, GameKey button)
{
    Widget* Target = NULL;
    UIWindowCapability* Self = NULL;
    Error Result = ResolveWindow(screen, windowId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!Widget_IsScreenRoot(Target) || (Self->_isMaximized && !Self->_isMinimized) || (Self->_dock == UIWindowDock_Docked))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWindow_StartMove: only a root window of its own that is not maximized moves.");
    }
    uint64_t TitleBarId = Self->_titleBarId;
    Result = UIScreen_TransferPress(screen, button, TitleBarId);
    // On top and active, as a press on its title bar makes it: Escape reaches it through the focus.
    if ((Result.Code == ErrorCode_Success) && UIScreen_TryGetWidgetById(screen, windowId, &Target))
    {
        Result = UIScreen_BringToTop(screen, Target);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ActivateWindow(screen, windowId);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    bool IsMoving = BeginGrabFrom(screen, windowId, UIWindowGrab_Move, UIWindowEdge_Count, button, UIScreen_GetPointerPosition(screen), true);
    if (!IsMoving)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWindow_StartMove: handing the press on changed the window: it does not move.");
    }
    return NotifyMove(screen, windowId, UIWindowMoveStage_Began, UIScreen_GetPointerPosition(screen));
}


// Public functions: behaviour helpers.
void UIWindowCapability_SetMetrics(UIWindowCapability* self, Widget* widget, const UIWindowMetrics* metrics)
{
    UIWindowMetrics Clean =
    {
        .TitleHeight = CleanLength(metrics->TitleHeight),
        .TitlePadding = CleanLength(metrics->TitlePadding),
        .IconSpacing = CleanLength(metrics->IconSpacing),
        .IconWidth = CleanLength(metrics->IconWidth),
        .ButtonSize = { .x = CleanLength(metrics->ButtonSize.x), .y = CleanLength(metrics->ButtonSize.y) },
        .ButtonSpacing = CleanLength(metrics->ButtonSpacing),
        .Border = CleanLength(metrics->Border),
        .ResizeMargin = CleanLength(metrics->ResizeMargin),
        .MinSize = { .x = CleanLength(metrics->MinSize.x), .y = CleanLength(metrics->MinSize.y) },
        .MinimizedWidth = CleanLength(metrics->MinimizedWidth),
        .ContentPadding = { .x = CleanLength(metrics->ContentPadding.x), .y = CleanLength(metrics->ContentPadding.y) },
        .ContentSpacing = CleanLength(metrics->ContentSpacing)
    };
    // There is always a title bar to drag by, and the edges leave most of it.
    Clean.TitleHeight = Math_MaxFloat(Clean.TitleHeight, UI_WINDOW_MIN_TITLE_HEIGHT);
    Clean.ResizeMargin = Math_MinFloat(Clean.ResizeMargin, Clean.TitleHeight * UI_WINDOW_MAX_EDGE_SHARE_OF_TITLE);
    if (Memory_IsEqual(&Clean, &self->_metrics, sizeof(Clean)))
    {
        return;
    }
    self->_metrics = Clean;
    Widget_InvalidateMeasure(widget);
}

Error UIWindow_Measure(Widget* widget, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    UIWindowCapability* Self = GetCapability(widget);
    UIScreen* Screen = Widget_GetScreen(widget);
    const UIWindowMetrics* Metrics = &Self->_metrics;
    float TitleNeeds = GetTitleBarNeeds(Self, Widget_IsScreenRoot(widget));
    float TitleHeight = GetTitleHeight(Self);
    float Border = GetBorder(Self);
    if (Self->_isMinimized)
    {
        *outSize = (Vector2){ .x = Math_MaxFloat(Metrics->MinimizedWidth, TitleNeeds), .y = Metrics->TitleHeight };
        return Error_CreateSuccess();
    }
    Vector2 Least =
    {
        .x = Math_MaxFloat(Math_MaxFloat(Metrics->MinSize.x, Self->_minimumSize.x), TitleNeeds),
        .y = Math_MaxFloat(Math_MaxFloat(Metrics->MinSize.y, Self->_minimumSize.y), TitleHeight + Border)
    };
    Widget* Content = NULL;
    if (!UIScreen_TryGetWidgetById(Screen, Self->_contentId, &Content) || !Widget_IsRendered(Content))
    {
        *outSize = Least;
        return Error_CreateSuccess();
    }
    Error Result = Error_CreateSuccess();
    if (Self->_fitAcross == UIWindowFit_Fit)
    {
        // As narrow as the content can be: measured with no room, each element at its own least width.
        Vector2 Narrowest = { .x = 0.0f, .y = 0.0f };
        Result = Widget_Measure(Content, context, 0.0f, &Narrowest);
        float Bar = (Self->_fitDown == UIWindowFit_Scroll) ? MeasureBar(Screen, Self->_areaId, UIScrollAxis_Vertical, context) : 0.0f;
        Least.x = Math_MaxFloat(Least.x, Narrowest.x + (2.0f * Border) + Bar);
    }
    if ((Result.Code == ErrorCode_Success) && (Self->_fitDown == UIWindowFit_Fit))
    {
        bool IsAcrossFitted = Self->_fitAcross == UIWindowFit_Fit;
        float Inner = IsAcrossFitted ? Math_MaxFloat(0.0f, Math_MaxFloat(availableWidth, Least.x) - (2.0f * Border)) : UI_MEASURE_UNBOUNDED_WIDTH;
        Vector2 AtWidth = { .x = 0.0f, .y = 0.0f };
        Result = Widget_Measure(Content, context, Inner, &AtWidth);
        float Bar = IsAcrossFitted ? 0.0f : MeasureBar(Screen, Self->_areaId, UIScrollAxis_Horizontal, context);
        Least.y = Math_MaxFloat(Least.y, AtWidth.y + TitleHeight + Border + Bar);
    }
    *outSize = Least;
    return Result;
}

UIElementState UIWindow_GetEdgeState(const Widget* widget, UIWindowEdge edge, Rectangle* outBox)
{
    *outBox = (Rectangle){ .x = 0.0f, .y = 0.0f, .width = 0.0f, .height = 0.0f };
    const UIWindowCapability* Self = &((const UIWindowLookBase*)(const void*)widget)->Window;
    if (((int32_t)edge < 0) || ((int32_t)edge >= (int32_t)UIWindowEdge_Count))
    {
        return UIElementState_Normal;
    }
    UIScreen* Screen = Widget_GetScreen(widget);
    Widget* Edge = NULL;
    if (!UIScreen_TryGetWidgetById(Screen, Self->_edgeIds[edge], &Edge) || !Widget_IsRendered(Edge))
    {
        return UIElementState_Normal;
    }
    Rectangle Pixels = UIScreen_GetWidgetPixelBox(Screen, widget);
    float Scale = Widget_GetEffectiveScale(widget);
    *outBox = GetEdgeBand(edge, Pixels.width / Scale, Pixels.height / Scale, Self->_metrics.ResizeMargin);
    if ((Self->_grab == UIWindowGrab_Resize) && (Self->_grabEdge == edge))
    {
        return UIElementState_Pressed;
    }
    return Widget_IsHovered(Edge) ? UIElementState_Hovered : UIElementState_Normal;
}

Error UIWindow_SetParts(UIScreen* screen, uint64_t windowId, const UIWindowParts* parts)
{
    Widget* Target = NULL;
    UIWindowCapability* Self = NULL;
    Error Result = ResolveWindow(screen, windowId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (Self->_titleBarId != WIDGET_ID_INVALID)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWindow_SetParts: the window has its parts already.");
    }
    Self->_frameId = parts->FrameId;
    Self->_titleBarId = parts->TitleBarId;
    Self->_areaId = parts->AreaId;
    Self->_contentId = parts->ContentId;
    for (size_t Index = 0U; Index < (size_t)UIWindowButton_Count; Index++)
    {
        Self->_buttonIds[Index] = parts->ButtonIds[Index];
    }
    for (size_t Index = 0U; Index < (size_t)UIWindowEdge_Count; Index++)
    {
        Self->_edgeIds[Index] = parts->EdgeIds[Index];
    }
    UserData Data = MakeIdData(windowId);
    uint64_t Subscription = UI_SCREEN_ID_INVALID;
    for (size_t Index = 0U; (Index < (size_t)UIWindowButton_Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        Result = UIScreen_Subscribe(screen, parts->ButtonIds[Index], UI_EVENT_KIND_MASK(UIEventKind_Widget), OnCaptionEvent, &Data, &Subscription);
    }
    if ((Result.Code == ErrorCode_Success) && UIScreen_TryGetWidgetById(screen, windowId, &Target))
    {
        Result = UIElement_PostCue(Target, UIElementCue_Open);
    }
    return (Result.Code == ErrorCode_Success) ? ApplyState(screen, windowId) : Result;
}

Error UIWindow_RegisterTypes(UIKit* kit, UIWidgetFactory* factory)
{
    WidgetTypeDescriptor Frame =
    {
        .Name = u8"WindowFrame",
        .StructSize = sizeof(UIWindowFrame),
        .VTable = &FrameVTable,
        .Constructor = Frame_Construct,
        .Capabilities = NULL,
        .CapabilityCount = 0U,
        .Properties = NULL,
        .PropertyCount = 0U,
        .IsFocusable = false
    };
    Error Result = UIWidgetFactory_RegisterType(factory, &Frame, &kit->_windowFrameType);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    WidgetTypeDescriptor TitleBar =
    {
        .Name = u8"WindowTitleBar",
        .StructSize = sizeof(UIWindowTitleBar),
        .VTable = &TitleBarVTable,
        .Constructor = TitleBar_Construct,
        .Capabilities = NULL,
        .CapabilityCount = 0U,
        .Properties = NULL,
        .PropertyCount = 0U,
        .IsFocusable = false
    };
    Result = UIWidgetFactory_RegisterType(factory, &TitleBar, &kit->_windowTitleType);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    WidgetTypeDescriptor Edge =
    {
        .Name = u8"WindowEdge",
        .StructSize = sizeof(UIWindowEdgeWidget),
        .VTable = &EdgeVTable,
        .Constructor = Edge_Construct,
        .Capabilities = NULL,
        .CapabilityCount = 0U,
        .Properties = NULL,
        .PropertyCount = 0U,
        .IsFocusable = false
    };
    return UIWidgetFactory_RegisterType(factory, &Edge, &kit->_windowEdgeType);
}


// Public functions: pure arithmetic.
Rectangle UIWindow_KeepReachable(Rectangle box, Rectangle workspace, float titleHeight, float buttonsWidth, float reachableWidth)
{
    Rectangle Result = box;
    float Draggable = Math_MaxFloat(0.0f, box.width - Math_MaxFloat(0.0f, buttonsWidth));
    float Needed = Math_MinFloat(Math_MaxFloat(0.0f, reachableWidth), Math_MinFloat(Draggable, Math_MaxFloat(0.0f, workspace.width)));
    // Off the left, the part at the right stays: the buttons and Needed of what drags. Off the right, Needed of what drags at the left.
    Result.x = Math_ClampFloat(box.x, workspace.x + Needed - Draggable, workspace.x + workspace.width - Needed);
    float Lowest = workspace.y + Math_MaxFloat(0.0f, workspace.height - Math_MaxFloat(0.0f, titleHeight));
    Result.y = Math_ClampFloat(box.y, workspace.y, Lowest);
    return Result;
}

Rectangle UIWindow_ResizeBox(Rectangle box, UIWindowEdge edge, Vector2 delta, Vector2 minimumSize, Rectangle workspace, float titleHeight)
{
    bool IsLeft = (edge == UIWindowEdge_Left) || (edge == UIWindowEdge_TopLeft) || (edge == UIWindowEdge_BottomLeft);
    bool IsRight = (edge == UIWindowEdge_Right) || (edge == UIWindowEdge_TopRight) || (edge == UIWindowEdge_BottomRight);
    bool IsTop = (edge == UIWindowEdge_Top) || (edge == UIWindowEdge_TopLeft) || (edge == UIWindowEdge_TopRight);
    bool IsBottom = (edge == UIWindowEdge_Bottom) || (edge == UIWindowEdge_BottomLeft) || (edge == UIWindowEdge_BottomRight);
    float Left = box.x;
    float Top = box.y;
    float Right = box.x + box.width;
    float Bottom = box.y + box.height;
    float WorkRight = workspace.x + workspace.width;
    float WorkBottom = workspace.y + workspace.height;
    float MinimumWidth = Math_MaxFloat(0.0f, minimumSize.x);
    float MinimumHeight = Math_MaxFloat(0.0f, minimumSize.y);
    // Each edge stops at the workspace's, or where it began when that lies beyond, and leaves the least size; the least size wins.
    if (IsLeft)
    {
        Left = Math_MinFloat(Math_MaxFloat(Left + delta.x, Math_MinFloat(workspace.x, Left)), Right - MinimumWidth);
    }
    if (IsRight)
    {
        Right = Math_MaxFloat(Math_MinFloat(Right + delta.x, Math_MaxFloat(WorkRight, Right)), Left + MinimumWidth);
    }
    if (IsTop)
    {
        float Lowest = Math_MinFloat(Bottom - MinimumHeight, Math_MaxFloat(WorkBottom - Math_MaxFloat(0.0f, titleHeight), Top));
        Top = Math_MinFloat(Math_MaxFloat(Top + delta.y, Math_MinFloat(workspace.y, Top)), Lowest);
    }
    if (IsBottom)
    {
        Bottom = Math_MaxFloat(Math_MinFloat(Bottom + delta.y, Math_MaxFloat(WorkBottom, Bottom)), Top + MinimumHeight);
    }
    return (Rectangle){ .x = Left, .y = Top, .width = Right - Left, .height = Bottom - Top };
}
