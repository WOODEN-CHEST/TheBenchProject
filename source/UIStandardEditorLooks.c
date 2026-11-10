#include "UIColorPicker.h"
#include "UIDialog.h"
#include "UIDragDrop.h"
#include "UIKit.h"
#include "UIList.h"
#include "UIScreen.h"
#include "UIStandardLooksInternal.h"
#include "UIToolbar.h"
#include "UITree.h"
#include "UIWidgetFactory.h"
#include "UIWindow.h"
#include "UIWorkspace.h"
#include "wr/WRMath.h"


/*
 * The kit's standard look (tbp:test_box) for the editor's elements: the list and the tree (a background; their rows are the kit's own
 * widgets), the dialog (a background; its parts are a label's, a column's and buttons), the colour picker's swatch (its popup is the kit's
 * own widgets), the tool bar (a background and its separators; its buttons are buttons), the window (its frame, its title bar with the icon
 * and the title, its content's background and an edge's highlight; its caption buttons are buttons), the workspace (its dock's background and
 * its areas' strips; its tabs, splitters and drop zones are the kit's own widgets), the render view (its host's picture, or its background
 * while there is none, and a frame over its edge), and the widget that shows a drag (the dragged item beside the pointer, and a frame over
 * the target that would take it). Every value comes from the screen's theme.
 */


// Types.
/* The standard list: a background, and the theme's metrics told to the behaviour. */
typedef struct ListBoxStruct
{
    UIListLookBase Base;
    UIElementLook Look;
} ListBox;

/* The standard tree: a background, and the theme's metrics told to the behaviour. */
typedef struct TreeBoxStruct
{
    UITreeLookBase Base;
    UIElementLook Look;
} TreeBox;

/* The standard dialog: a background, and the theme's metrics told to the behaviour (it lays its parts out). */
typedef struct DialogBoxStruct
{
    UIDialogLookBase Base;
    UIElementLook Look;
} DialogBox;

/* The standard colour picker's swatch: the colour over a checkerboard where it is not opaque (a hyphen while mixed), its frame on top. */
typedef struct ColorPickerBoxStruct
{
    UIColorPickerLookBase Base;
    UIElementLook Look;
} ColorPickerBox;

/* The standard tool bar: a background, and a line in the middle of each separator's room, as long across the bar as its buttons are. */
typedef struct ToolbarBoxStruct
{
    UIToolbarLookBase Base;
    UIElementLook Look;
} ToolbarBox;

/* The standard window: its frame, its title bar with the icon and the title (focused while the window is active), the content's background
 * (not while minimized; alone while docked) and the highlight of an edge under the pointer or dragged; the theme's metrics told to the
 * behaviour. */
typedef struct WindowBoxStruct
{
    UIWindowLookBase Base;
    UIElementLook Look;
    UIElementText Title;
    UIElementIconView Icon;
} WindowBox;

/* The standard workspace: the dock's background, and behind each area's tabs its strip (focused while the area's window is active); the
 * theme's metrics told to the behaviour. Its bars, tabs, splitters and windows draw themselves. */
typedef struct WorkspaceBoxStruct
{
    UIWorkspaceLookBase Base;
    UIElementLook Look;
} WorkspaceBox;

/* The standard render view: its host's picture stretched to its box (its background while there is none) and a frame over its edge, hovered,
 * focused or entered. */
typedef struct RenderViewBoxStruct
{
    UIRenderViewLookBase Base;
    UIElementLook Look;
} RenderViewBox;

/* What shows a drag: a root of the overlay layer over the whole surface, drawing the dragged item beside the pointer and a frame over the
 * target that would take it, clipped as the target is. A plain widget type, made by the screen (UIDragDrop.h) with no arguments. */
typedef struct DragGhostStruct
{
    Widget Base;
    UIElementLook Look;
    UIElementText Text;
    UITextContent Content;
    bool IsShowing;
    /* Where the dragged item and the target's frame are, in surface pixels, as the last layout put them. */
    Rectangle GhostBox;
    bool HasTargetBox;
    Rectangle TargetBox;
    Rectangle TargetClip;
} DragGhost;


// Static functions: shared.
static float RoundToPixel(float value)
{
    return Math_FloorFloat(value + 0.5f);
}

static Rectangle Intersect(Rectangle a, Rectangle b)
{
    float Left = Math_MaxFloat(a.x, b.x);
    float Top = Math_MaxFloat(a.y, b.y);
    float Right = Math_MinFloat(a.x + a.width, b.x + b.width);
    float Bottom = Math_MinFloat(a.y + a.height, b.y + b.height);
    return (Rectangle){ .x = Left, .y = Top, .width = Math_MaxFloat(0.0f, Right - Left), .height = Math_MaxFloat(0.0f, Bottom - Top) };
}


// Static functions: the list.
static Error ListBox_Construct(void* self, void* args)
{
    return UIElementLook_Construct(&((ListBox*)self)->Look, UIElementKind_List, ((const UIListArgs*)args)->Options->Element.Variant);
}

static Error ListBox_Update(void* self, ProgramTime time)
{
    ListBox* Box = self;
    Widget* Base = &Box->Base.Base;
    UIScreen* Screen = Widget_GetScreen(Base);
    UIElementLook_Update(&Box->Look, Screen, UIListCapability_GetState(&Box->Base.List, Base), time.PassedTime);
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Box->Look, Screen);
    UIListMetrics Metrics =
    {
        .Padding = UIThemeStyle_GetVector(Style, UIElementKind_List, UIListPart_Padding),
        .RowHeight = UIThemeStyle_GetNumber(Style, UIElementKind_List, UIListPart_RowHeight),
        .RowPadding = UIThemeStyle_GetNumber(Style, UIElementKind_List, UIListPart_RowPadding),
        .CellSize = UIThemeStyle_GetNumber(Style, UIElementKind_List, UIListPart_CellSize),
        .InsertionThickness = UIThemeStyle_GetNumber(Style, UIElementKind_List, UIListPart_InsertionThickness)
    };
    UIListCapability_SetMetrics(&Box->Base.List, Base, &Metrics);
    return Error_CreateSuccess();
}

static Error ListBox_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)context;
    (void)availableWidth;
    UIListCapability_Measure(&((ListBox*)self)->Base.List, outSize);
    return Error_CreateSuccess();
}

static Error ListBox_Render(void* self, UIRenderContext* context)
{
    ListBox* Box = self;
    Rectangle Pixels = UIRenderContext_GetPixelBox(context);
    if (UIElement_IsBoxEmpty(Pixels))
    {
        return Error_CreateSuccess();
    }
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Box->Look, Widget_GetScreen(&Box->Base.Base));
    return UIElementLook_DrawBox(&Box->Look, Style, UIListPart_Background, context, Pixels, Widget_GetEffectiveScale(&Box->Base.Base));
}

static UIElementLook* ListBox_GetThemeLook(void* self)
{
    return &((ListBox*)self)->Look;
}


// Static functions: the tree.
static Error TreeBox_Construct(void* self, void* args)
{
    return UIElementLook_Construct(&((TreeBox*)self)->Look, UIElementKind_Tree, ((const UITreeArgs*)args)->Options->Element.Variant);
}

static Error TreeBox_Update(void* self, ProgramTime time)
{
    TreeBox* Box = self;
    Widget* Base = &Box->Base.Base;
    UIScreen* Screen = Widget_GetScreen(Base);
    UIElementLook_Update(&Box->Look, Screen, UITreeCapability_GetState(&Box->Base.Tree, Base), time.PassedTime);
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Box->Look, Screen);
    UITreeMetrics Metrics =
    {
        .Padding = UIThemeStyle_GetVector(Style, UIElementKind_Tree, UITreePart_Padding),
        .RowHeight = UIThemeStyle_GetNumber(Style, UIElementKind_Tree, UITreePart_RowHeight),
        .RowPadding = UIThemeStyle_GetNumber(Style, UIElementKind_Tree, UITreePart_RowPadding),
        .Indent = UIThemeStyle_GetNumber(Style, UIElementKind_Tree, UITreePart_Indent),
        .CellSize = UIThemeStyle_GetNumber(Style, UIElementKind_Tree, UITreePart_CellSize),
        .InsertionThickness = UIThemeStyle_GetNumber(Style, UIElementKind_Tree, UITreePart_InsertionThickness)
    };
    UITreeCapability_SetMetrics(&Box->Base.Tree, Base, &Metrics);
    return Error_CreateSuccess();
}

static Error TreeBox_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)context;
    (void)availableWidth;
    UITreeCapability_Measure(&((TreeBox*)self)->Base.Tree, outSize);
    return Error_CreateSuccess();
}

static Error TreeBox_Render(void* self, UIRenderContext* context)
{
    TreeBox* Box = self;
    Rectangle Pixels = UIRenderContext_GetPixelBox(context);
    if (UIElement_IsBoxEmpty(Pixels))
    {
        return Error_CreateSuccess();
    }
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Box->Look, Widget_GetScreen(&Box->Base.Base));
    return UIElementLook_DrawBox(&Box->Look, Style, UITreePart_Background, context, Pixels, Widget_GetEffectiveScale(&Box->Base.Base));
}

static UIElementLook* TreeBox_GetThemeLook(void* self)
{
    return &((TreeBox*)self)->Look;
}


// Static functions: the dialog.
static Error DialogBox_Construct(void* self, void* args)
{
    return UIElementLook_Construct(&((DialogBox*)self)->Look, UIElementKind_Dialog, ((const UIDialogArgs*)args)->Options->Element.Variant);
}

static Error DialogBox_Update(void* self, ProgramTime time)
{
    DialogBox* Box = self;
    Widget* Base = &Box->Base.Base;
    UIScreen* Screen = Widget_GetScreen(Base);
    UIElementLook_Update(&Box->Look, Screen, UIElementState_Normal, time.PassedTime);
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Box->Look, Screen);
    UIDialogMetrics Metrics =
    {
        .Padding = UIThemeStyle_GetVector(Style, UIElementKind_Dialog, UIDialogPart_Padding),
        .Spacing = UIThemeStyle_GetNumber(Style, UIElementKind_Dialog, UIDialogPart_Spacing),
        .ButtonSpacing = UIThemeStyle_GetNumber(Style, UIElementKind_Dialog, UIDialogPart_ButtonSpacing),
        .ButtonWidth = UIThemeStyle_GetNumber(Style, UIElementKind_Dialog, UIDialogPart_ButtonWidth),
        .MinSize = UIThemeStyle_GetVector(Style, UIElementKind_Dialog, UIDialogPart_MinSize),
        .MaxWidth = UIThemeStyle_GetNumber(Style, UIElementKind_Dialog, UIDialogPart_MaxWidth)
    };
    UIDialogCapability_SetMetrics(&Box->Base.Dialog, Base, &Metrics);
    return Error_CreateSuccess();
}

static Error DialogBox_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    return UIDialog_Measure(&((DialogBox*)self)->Base.Base, context, availableWidth, outSize);
}

static Error DialogBox_Render(void* self, UIRenderContext* context)
{
    DialogBox* Box = self;
    Rectangle Pixels = UIRenderContext_GetPixelBox(context);
    if (UIElement_IsBoxEmpty(Pixels))
    {
        return Error_CreateSuccess();
    }
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Box->Look, Widget_GetScreen(&Box->Base.Base));
    return UIElementLook_DrawBox(&Box->Look, Style, UIDialogPart_Background, context, Pixels, Widget_GetEffectiveScale(&Box->Base.Base));
}

static UIElementLook* DialogBox_GetThemeLook(void* self)
{
    return &((DialogBox*)self)->Look;
}


// Static functions: the colour picker's swatch.
static Error ColorPickerBox_Construct(void* self, void* args)
{
    return UIElementLook_Construct(&((ColorPickerBox*)self)->Look, UIElementKind_ColorPicker,
        ((const UIColorPickerArgs*)args)->Options->Element.Variant);
}

static Error ColorPickerBox_Update(void* self, ProgramTime time)
{
    ColorPickerBox* Box = self;
    UIElementLook_Update(&Box->Look, Widget_GetScreen(&Box->Base.Base), UIColorPickerCapability_GetState(&Box->Base.Picker, &Box->Base.Base),
        time.PassedTime);
    return Error_CreateSuccess();
}

static Error ColorPickerBox_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)context;
    (void)availableWidth;
    ColorPickerBox* Box = self;
    *outSize = UIThemeStyle_GetVector(UIElementLook_GetStyle(&Box->Look, Widget_GetScreen(&Box->Base.Base)), UIElementKind_ColorPicker,
        UIColorPickerPart_MinSize);
    return Error_CreateSuccess();
}

/* Two colours of squares inside a box, behind a colour that is not opaque. */
static void DrawSwatchChecker(UIRenderContext* context, const UIThemeStyle* style, Rectangle box, float scale)
{
    float Side = Math_MaxFloat(1.0f, RoundToPixel(UIThemeStyle_GetNumber(style, UIElementKind_ColorPicker, UIColorPickerPart_CheckerSize) * scale));
    const UIStateColors* Light = UIThemeStyle_GetColors(style, UIColorPickerPart_CheckerLight);
    const UIStateColors* Dark = UIThemeStyle_GetColors(style, UIColorPickerPart_CheckerDark);
    UIRenderContext_RenderPixelRectangle(context, box, (Light != NULL) ? UIStateColors_Resolve(Light, UIElementState_Normal, WHITE) : WHITE);
    Color DarkColor = (Dark != NULL) ? UIStateColors_Resolve(Dark, UIElementState_Normal, GRAY) : GRAY;
    for (float Y = 0.0f; Y < box.height; Y += Side)
    {
        bool IsOdd = ((int32_t)(Y / Side) % 2) != 0;
        for (float X = IsOdd ? Side : 0.0f; X < box.width; X += 2.0f * Side)
        {
            Rectangle Cell = { .x = box.x + X, .y = box.y + Y, .width = Math_MinFloat(Side, box.width - X), .height = Math_MinFloat(Side, box.height - Y) };
            UIRenderContext_RenderPixelRectangle(context, Cell, DarkColor);
        }
    }
}

static Error ColorPickerBox_Render(void* self, UIRenderContext* context)
{
    ColorPickerBox* Box = self;
    Rectangle Pixels = UIRenderContext_GetPixelBox(context);
    if (UIElement_IsBoxEmpty(Pixels))
    {
        return Error_CreateSuccess();
    }
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Box->Look, Widget_GetScreen(&Box->Base.Base));
    float Scale = Widget_GetEffectiveScale(&Box->Base.Base);
    const UIColorPickerCapability* Picker = &Box->Base.Picker;
    if (Picker->_isMixed)
    {
        // A hyphen in the mixed colour over the checkerboard: no one colour to show.
        DrawSwatchChecker(context, Style, Pixels, Scale);
        float Width = RoundToPixel(Pixels.width * 0.4f);
        float Height = Math_MaxFloat(1.0f, RoundToPixel(2.0f * Scale));
        Rectangle Bar = { .x = Pixels.x + RoundToPixel((Pixels.width - Width) * 0.5f), .y = Pixels.y + RoundToPixel((Pixels.height - Height) * 0.5f),
            .width = Width, .height = Height };
        UIRenderContext_RenderPixelRectangle(context, Bar, UIElementLook_BlendColors(&Box->Look, UIThemeStyle_GetColors(Style, UIColorPickerPart_Mixed), WHITE));
    }
    else
    {
        Color Shown = UIColorPickerCapability_GetShownColor(Picker);
        if (Shown.a < 255U)
        {
            DrawSwatchChecker(context, Style, Pixels, Scale);
        }
        UIRenderContext_RenderPixelRectangle(context, Pixels, Shown);
    }
    return UIElementLook_DrawBox(&Box->Look, Style, UIColorPickerPart_Swatch, context, Pixels, Scale);
}

static UIElementLook* ColorPickerBox_GetThemeLook(void* self)
{
    return &((ColorPickerBox*)self)->Look;
}


// Static functions: the tool bar.
static Error ToolbarBox_Construct(void* self, void* args)
{
    return UIElementLook_Construct(&((ToolbarBox*)self)->Look, UIElementKind_Toolbar, ((const UIToolbarArgs*)args)->Options->Element.Variant);
}

static Error ToolbarBox_Update(void* self, ProgramTime time)
{
    ToolbarBox* Box = self;
    UIElementLook_Update(&Box->Look, Widget_GetScreen(&Box->Base.Base), UIElementState_Normal, time.PassedTime);
    return Error_CreateSuccess();
}

static UIToolbarMetrics ToolbarBox_GetMetrics(const ToolbarBox* box)
{
    const UIThemeStyle* Style = UIElementLook_GetStyle(&box->Look, Widget_GetScreen(&box->Base.Base));
    return (UIToolbarMetrics)
    {
        .Padding = UIThemeStyle_GetVector(Style, UIElementKind_Toolbar, UIToolbarPart_Padding),
        .Spacing = UIThemeStyle_GetNumber(Style, UIElementKind_Toolbar, UIToolbarPart_Spacing),
        .ButtonSize = UIThemeStyle_GetVector(Style, UIElementKind_Toolbar, UIToolbarPart_ButtonSize),
        .SeparatorSpacing = UIThemeStyle_GetNumber(Style, UIElementKind_Toolbar, UIToolbarPart_SeparatorSpacing)
    };
}

static Error ToolbarBox_Layout(void* self, const UILayoutContext* context)
{
    ToolbarBox* Box = self;
    UIToolbarMetrics Metrics = ToolbarBox_GetMetrics(Box);
    return UIToolbar_Arrange(&Box->Base.Base, context, &Metrics);
}

static Error ToolbarBox_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)availableWidth;
    ToolbarBox* Box = self;
    UIToolbarMetrics Metrics = ToolbarBox_GetMetrics(Box);
    return UIToolbar_Measure(&Box->Base.Base, context, &Metrics, outSize);
}

/* The background, then each separator's line: centred in its room, across the bar between its padding. */
static Error ToolbarBox_Render(void* self, UIRenderContext* context)
{
    ToolbarBox* Box = self;
    Rectangle Pixels = UIRenderContext_GetPixelBox(context);
    if (UIElement_IsBoxEmpty(Pixels))
    {
        return Error_CreateSuccess();
    }
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Box->Look, Widget_GetScreen(&Box->Base.Base));
    float Scale = Widget_GetEffectiveScale(&Box->Base.Base);
    Error Result = UIElementLook_DrawBox(&Box->Look, Style, UIToolbarPart_Background, context, Pixels, Scale);
    const UIToolbarCapability* Bar = &Box->Base.Toolbar;
    bool IsVertical = UIToolbarCapability_IsVertical(Bar);
    Vector2 Padding = UIThemeStyle_GetVector(Style, UIElementKind_Toolbar, UIToolbarPart_Padding);
    float Thickness = Math_MaxFloat(1.0f, RoundToPixel(UIThemeStyle_GetNumber(Style, UIElementKind_Toolbar, UIToolbarPart_SeparatorThickness) * Scale));
    float AcrossStart = RoundToPixel((IsVertical ? Padding.x : Padding.y) * Scale);
    float AcrossLength = (IsVertical ? Pixels.width : Pixels.height) - (2.0f * AcrossStart);
    for (size_t Index = 0U; (Index < UIToolbarCapability_GetEntryCount(Bar)) && (Result.Code == ErrorCode_Success) && (AcrossLength > 0.0f); Index++)
    {
        const UIToolbarEntry* Entry = UIToolbarCapability_GetEntryAt(Bar, Index);
        if (Entry->Kind != UIToolbarItemKind_Separator)
        {
            continue;
        }
        float Along = RoundToPixel(((Entry->Start + (Entry->Length * 0.5f)) * Scale) - (Thickness * 0.5f));
        Rectangle Line = IsVertical ? (Rectangle){ .x = Pixels.x + AcrossStart, .y = Pixels.y + Along, .width = AcrossLength, .height = Thickness }
            : (Rectangle){ .x = Pixels.x + Along, .y = Pixels.y + AcrossStart, .width = Thickness, .height = AcrossLength };
        Result = UIElementLook_DrawBox(&Box->Look, Style, UIToolbarPart_Separator, context, Line, Scale);
    }
    return Result;
}

static UIElementLook* ToolbarBox_GetThemeLook(void* self)
{
    return &((ToolbarBox*)self)->Look;
}


// Static functions: the window.
static Error WindowBox_Construct(void* self, void* args)
{
    WindowBox* Box = self;
    const UIWindowArgs* Arguments = args;
    Error Result = UIElementLook_Construct(&Box->Look, UIElementKind_Window, Arguments->Options->Element.Variant);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = UIElementText_Construct(&Box->Title, UIKit_GetTextFactory(Arguments->Kit), UIKit_GetAssets(Arguments->Kit),
        UIKit_GetLogger(Arguments->Kit));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = UIElementIconView_Construct(&Box->Icon, UIKit_GetTextFactory(Arguments->Kit), UIKit_GetAssets(Arguments->Kit),
        UIKit_GetLogger(Arguments->Kit));
    if (Result.Code != ErrorCode_Success)
    {
        Error Cleanup = UIElementText_Deconstruct(&Box->Title);
        Error_Deconstruct(&Cleanup);
    }
    return Result;
}

static Error WindowBox_Deconstruct(void* self)
{
    WindowBox* Box = self;
    Error Result = UIElementIconView_Deconstruct(&Box->Icon);
    Error TextResult = UIElementText_Deconstruct(&Box->Title);
    if (Result.Code == ErrorCode_Success)
    {
        return TextResult;
    }
    Error_Deconstruct(&TextResult);
    return Result;
}

static Error WindowBox_Update(void* self, ProgramTime time)
{
    WindowBox* Box = self;
    Widget* Base = &Box->Base.Base;
    UIScreen* Screen = Widget_GetScreen(Base);
    const UIWindowCapability* Window = &Box->Base.Window;
    UIElementLook_Update(&Box->Look, Screen, UIWindowCapability_IsActive(Window) ? UIElementState_Focused : UIElementState_Normal,
        time.PassedTime);
    Error Result = UIElementText_Refresh(&Box->Title, Base, &Window->_title);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIElementIconView_Refresh(&Box->Icon, Base, &Window->_icon);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIElementIconView_Update(&Box->Icon, time);
    }
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Box->Look, Screen);
    UIWindowMetrics Metrics =
    {
        .TitleHeight = UIThemeStyle_GetNumber(Style, UIElementKind_Window, UIWindowPart_TitleHeight),
        .TitlePadding = UIThemeStyle_GetNumber(Style, UIElementKind_Window, UIWindowPart_TitlePadding),
        .IconSpacing = UIThemeStyle_GetNumber(Style, UIElementKind_Window, UIWindowPart_IconSpacing),
        .IconWidth = UIElementIconView_GetPixelSize(&Box->Icon, Screen).x / Widget_GetEffectiveScale(Base),
        .ButtonSize = UIThemeStyle_GetVector(Style, UIElementKind_Window, UIWindowPart_ButtonSize),
        .ButtonSpacing = UIThemeStyle_GetNumber(Style, UIElementKind_Window, UIWindowPart_ButtonSpacing),
        .Border = UIThemeStyle_GetNumber(Style, UIElementKind_Window, UIWindowPart_Border),
        .ResizeMargin = UIThemeStyle_GetNumber(Style, UIElementKind_Window, UIWindowPart_ResizeMargin),
        .MinSize = UIThemeStyle_GetVector(Style, UIElementKind_Window, UIWindowPart_MinSize),
        .MinimizedWidth = UIThemeStyle_GetNumber(Style, UIElementKind_Window, UIWindowPart_MinimizedWidth),
        .ContentPadding = UIThemeStyle_GetVector(Style, UIElementKind_Window, UIWindowPart_ContentPadding),
        .ContentSpacing = UIThemeStyle_GetNumber(Style, UIElementKind_Window, UIWindowPart_ContentSpacing)
    };
    UIWindowCapability_SetMetrics(&Box->Base.Window, Base, &Metrics);
    return Result;
}

static Error WindowBox_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    return UIWindow_Measure(&((WindowBox*)self)->Base.Base, context, availableWidth, outSize);
}

/* The icon and the title in the title bar: the icon after the padding, the title after it, cut where the caption buttons begin. */
static Error WindowBox_RenderTitle(const WindowBox* box, UIRenderContext* context, const UIThemeStyle* style, Rectangle bar, float scale)
{
    const UIScreen* Screen = Widget_GetScreen(&box->Base.Base);
    const UIWindowCapability* Window = &box->Base.Window;
    Color TextColor = UIElementLook_BlendColors(&box->Look, UIThemeStyle_GetColors(style, UIWindowPart_TitleText), WHITE);
    Vector2 Icon = UIElementIconView_GetPixelSize(&box->Icon, Screen);
    Vector2 Text = UIElementText_GetPixelSize(&box->Title, Screen);
    float X = bar.x + RoundToPixel(Window->_metrics.TitlePadding * scale);
    float End = bar.x + RoundToPixel(UIWindowCapability_GetTitleEnd(Window) * scale);
    if (!(End > X))
    {
        return Error_CreateSuccess();
    }
    Rectangle Span = { .x = X, .y = bar.y, .width = End - X, .height = bar.height };
    RenderContext_PushClip(UIRenderContext_GetRenderContext(context), Span);
    Error Result = UIElementIconView_Draw(&box->Icon, context, X, bar.y + RoundToPixel((bar.height - Icon.y) * 0.5f), TextColor);
    if (UIElementIconView_IsShowing(&box->Icon))
    {
        X += Icon.x + RoundToPixel(Window->_metrics.IconSpacing * scale);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIElementText_Draw(&box->Title, context, X, bar.y + RoundToPixel((bar.height - Text.y) * 0.5f), TextColor);
    }
    RenderContext_PopClip(UIRenderContext_GetRenderContext(context));
    return Result;
}

/* Each edge's highlight while it shows hovered or pressed; a theme with no such part draws none. */
static Error WindowBox_RenderEdges(const WindowBox* box, UIRenderContext* context, const UIThemeStyle* style, Rectangle pixels, float scale)
{
    const UIBoxStyle* Highlight = UIThemeStyle_GetBox(style, UIWindowPart_ResizeEdge);
    Error Result = Error_CreateSuccess();
    for (int32_t Edge = 0; (Edge < (int32_t)UIWindowEdge_Count) && (Highlight != NULL) && (Result.Code == ErrorCode_Success); Edge++)
    {
        Rectangle Band;
        UIElementState State = UIWindow_GetEdgeState(&box->Base.Base, (UIWindowEdge)Edge, &Band);
        if (State == UIElementState_Normal)
        {
            continue;
        }
        Rectangle BandPixels = { .x = pixels.x + RoundToPixel(Band.x * scale), .y = pixels.y + RoundToPixel(Band.y * scale),
            .width = RoundToPixel(Band.width * scale), .height = RoundToPixel(Band.height * scale) };
        UIBoxPaint Paint = UIBoxStyle_Resolve(Highlight, State);
        Result = UIBoxStyle_Draw(Highlight, context, BandPixels, &Paint, scale);
    }
    return Result;
}

static Error WindowBox_Render(void* self, UIRenderContext* context)
{
    WindowBox* Box = self;
    Rectangle Pixels = UIRenderContext_GetPixelBox(context);
    if (UIElement_IsBoxEmpty(Pixels))
    {
        return Error_CreateSuccess();
    }
    const UIWindowCapability* Window = &Box->Base.Window;
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Box->Look, Widget_GetScreen(&Box->Base.Base));
    float Scale = Widget_GetEffectiveScale(&Box->Base.Base);
    if (UIWindowCapability_GetDock(Window) == UIWindowDock_Docked)
    {
        // Its dock's tab stands for its title bar and its dock's frame for its own: only the content's background shows.
        return UIElementLook_DrawBox(&Box->Look, Style, UIWindowPart_ContentBackground, context, Pixels, Scale);
    }
    Error Result = UIElementLook_DrawBox(&Box->Look, Style, UIWindowPart_Frame, context, Pixels, Scale);
    float TitleHeight = Math_MinFloat(Pixels.height, RoundToPixel(Window->_metrics.TitleHeight * Scale));
    Rectangle Bar = { .x = Pixels.x, .y = Pixels.y, .width = Pixels.width, .height = TitleHeight };
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIElementLook_DrawBox(&Box->Look, Style, UIWindowPart_TitleBar, context, Bar, Scale);
    }
    float Border = RoundToPixel(Window->_metrics.Border * Scale);
    Rectangle Content = { .x = Pixels.x + Border, .y = Pixels.y + TitleHeight, .width = Pixels.width - (2.0f * Border),
        .height = Pixels.height - TitleHeight - Border };
    if ((Result.Code == ErrorCode_Success) && !UIWindowCapability_IsMinimized(Window) && !UIElement_IsBoxEmpty(Content))
    {
        Result = UIElementLook_DrawBox(&Box->Look, Style, UIWindowPart_ContentBackground, context, Content, Scale);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = WindowBox_RenderTitle(Box, context, Style, Bar, Scale);
    }
    return (Result.Code == ErrorCode_Success) ? WindowBox_RenderEdges(Box, context, Style, Pixels, Scale) : Result;
}

static UIElementLook* WindowBox_GetThemeLook(void* self)
{
    return &((WindowBox*)self)->Look;
}


// Static functions: the workspace.
static Error WorkspaceBox_Construct(void* self, void* args)
{
    WorkspaceBox* Box = self;
    const UIWorkspaceArgs* Arguments = args;
    return UIElementLook_Construct(&Box->Look, UIElementKind_Workspace, Arguments->Options->Element.Variant);
}

static Error WorkspaceBox_Update(void* self, ProgramTime time)
{
    WorkspaceBox* Box = self;
    Widget* Base = &Box->Base.Base;
    UIScreen* Screen = Widget_GetScreen(Base);
    UIElementLook_Update(&Box->Look, Screen, UIElementState_Normal, time.PassedTime);
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Box->Look, Screen);
    UIWorkspaceMetrics Metrics =
    {
        .TabHeight = UIThemeStyle_GetNumber(Style, UIElementKind_Workspace, UIWorkspacePart_TabHeight),
        .TabPadding = UIThemeStyle_GetNumber(Style, UIElementKind_Workspace, UIWorkspacePart_TabPadding),
        .TabSpacing = UIThemeStyle_GetNumber(Style, UIElementKind_Workspace, UIWorkspacePart_TabSpacing),
        .IconSpacing = UIThemeStyle_GetNumber(Style, UIElementKind_Workspace, UIWorkspacePart_IconSpacing),
        .TabMaxWidth = UIThemeStyle_GetNumber(Style, UIElementKind_Workspace, UIWorkspacePart_TabMaxWidth),
        .SplitterThickness = UIThemeStyle_GetNumber(Style, UIElementKind_Workspace, UIWorkspacePart_SplitterThickness),
        .GuideSize = UIThemeStyle_GetNumber(Style, UIElementKind_Workspace, UIWorkspacePart_GuideSize)
    };
    UIWorkspaceCapability_SetMetrics(&Box->Base.Workspace, Base, &Metrics);
    return Error_CreateSuccess();
}

static Error WorkspaceBox_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)self;
    (void)context;
    (void)availableWidth;
    *outSize = (Vector2){ .x = 0.0f, .y = 0.0f }; // It covers its surface.
    return Error_CreateSuccess();
}

static Error WorkspaceBox_Render(void* self, UIRenderContext* context)
{
    WorkspaceBox* Box = self;
    const UIWorkspaceCapability* Workspace = &Box->Base.Workspace;
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Box->Look, Widget_GetScreen(&Box->Base.Base));
    float Scale = Widget_GetEffectiveScale(&Box->Base.Base);
    Error Result = Error_CreateSuccess();
    if (!UIElement_IsBoxEmpty(Workspace->_dockArea))
    {
        Result = UIElementLook_DrawBox(&Box->Look, Style, UIWorkspacePart_Background, context, Workspace->_dockArea, Scale);
    }
    const UIBoxStyle* Strip = UIThemeStyle_GetBox(Style, UIWorkspacePart_Strip);
    for (int32_t Node = 0; (Node < (int32_t)UI_DOCK_TREE_MAX_NODES) && (Strip != NULL) && (Result.Code == ErrorCode_Success); Node++)
    {
        Rectangle StripBox;
        bool IsActive = false;
        if (UIWorkspaceCapability_GetLeaf(Workspace, Node, &StripBox, &IsActive) && !UIElement_IsBoxEmpty(StripBox))
        {
            UIBoxPaint Paint = UIBoxStyle_Resolve(Strip, IsActive ? UIElementState_Focused : UIElementState_Normal);
            Result = UIBoxStyle_Draw(Strip, context, StripBox, &Paint, Scale);
        }
    }
    return Result;
}

static UIElementLook* WorkspaceBox_GetThemeLook(void* self)
{
    return &((WorkspaceBox*)self)->Look;
}


// Static functions: the render view.
static Error RenderViewBox_Construct(void* self, void* args)
{
    return UIElementLook_Construct(&((RenderViewBox*)self)->Look, UIElementKind_RenderView,
        ((const UIRenderViewArgs*)args)->Options->Element.Variant);
}

/* Entered shows over focused, focused over hovered: whether the keys are its host's is what matters most, and the pointer passing over a
 * focused view does not hide that it has the focus. */
static Error RenderViewBox_Update(void* self, ProgramTime time)
{
    RenderViewBox* Box = self;
    Widget* Base = &Box->Base.Base;
    UIElementState State = Widget_IsEntered(Base) ? UIElementState_Entered
        : (Widget_IsFocused(Base) ? UIElementState_Focused : (Widget_IsHovered(Base) ? UIElementState_Hovered : UIElementState_Normal));
    UIElementLook_Update(&Box->Look, Widget_GetScreen(Base), State, time.PassedTime);
    return Error_CreateSuccess();
}

static Error RenderViewBox_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)context;
    (void)availableWidth;
    RenderViewBox* Box = self;
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Box->Look, Widget_GetScreen(&Box->Base.Base));
    *outSize = UIThemeStyle_GetVector(Style, UIElementKind_RenderView, UIRenderViewPart_MinSize);
    return Error_CreateSuccess();
}

/* The picture over the whole box, upright (a render target's rows run bottom-up), else the background; the frame on top. */
static Error RenderViewBox_Render(void* self, UIRenderContext* context)
{
    RenderViewBox* Box = self;
    Rectangle Pixels = UIRenderContext_GetPixelBox(context);
    if (UIElement_IsBoxEmpty(Pixels))
    {
        return Error_CreateSuccess();
    }
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Box->Look, Widget_GetScreen(&Box->Base.Base));
    float Scale = Widget_GetEffectiveScale(&Box->Base.Base);
    Texture2D Picture;
    bool IsBottomUp = false;
    Error Result = Error_CreateSuccess();
    if (UIRenderViewCapability_GetTexture(&Box->Base.View, &Picture, &IsBottomUp) && (Picture.width > 0) && (Picture.height > 0))
    {
        float Height = (float)Picture.height;
        Rectangle Source = { .x = 0.0f, .y = 0.0f, .width = (float)Picture.width, .height = IsBottomUp ? -Height : Height };
        UIRenderContext_RenderPixelTexture(context, Picture, Source, Pixels, WHITE);
    }
    else
    {
        Result = UIElementLook_DrawBox(&Box->Look, Style, UIRenderViewPart_Background, context, Pixels, Scale);
    }
    return (Result.Code == ErrorCode_Success) ? UIElementLook_DrawBox(&Box->Look, Style, UIRenderViewPart_Frame, context, Pixels, Scale)
        : Result;
}

static UIElementLook* RenderViewBox_GetThemeLook(void* self)
{
    return &((RenderViewBox*)self)->Look;
}


// Static functions: what shows a drag.
static Error DragGhost_Construct(void* widget, void* args)
{
    (void)args;
    DragGhost* Ghost = widget;
    UIKit* Kit = UIKit_FromScreen(Widget_GetScreen(&Ghost->Base));
    if (Kit == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIStandardLooks: what shows a drag needs its screen's kit.");
    }
    Error Result = UIElementLook_Construct(&Ghost->Look, UIElementKind_Drag, NULL);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UITextContent_Construct(&Ghost->Content, NULL);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = UIElementText_Construct(&Ghost->Text, UIKit_GetTextFactory(Kit), UIKit_GetAssets(Kit), UIKit_GetLogger(Kit));
    if (Result.Code != ErrorCode_Success)
    {
        UITextContent_Deconstruct(&Ghost->Content);
        return Result;
    }
    Ghost->IsShowing = false;
    Ghost->GhostBox = (Rectangle){ .x = 0.0f, .y = 0.0f, .width = 0.0f, .height = 0.0f };
    Ghost->HasTargetBox = false;
    Ghost->TargetBox = Ghost->GhostBox;
    Ghost->TargetClip = Ghost->GhostBox;
    UIPlacement Whole = UIPlacement_Stretch(UIInsets_All(0.0f));
    return Widget_SetPlacement(widget, &Whole);
}

static Error DragGhost_OnDeconstruct(void* self)
{
    DragGhost* Ghost = self;
    UITextContent_Deconstruct(&Ghost->Content);
    return UIElementText_Deconstruct(&Ghost->Text);
}

static Error DragGhost_Update(void* self, ProgramTime time)
{
    DragGhost* Ghost = self;
    UIScreen* Screen = Widget_GetScreen(&Ghost->Base);
    UIDragSession Session;
    Ghost->IsShowing = UIDragDrop_GetSession(Screen, &Session);
    UIElementState State = UIElementState_Normal;
    if (Ghost->IsShowing)
    {
        State = UIElementState_Choose(Session.TargetState != UIDragTargetState_Refusing, false, false, false,
            Session.TargetState == UIDragTargetState_Accepting, false, false);
        Error Result = UITextContent_Set(&Ghost->Content, Session.Payload.Text, NULL);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    UIElementLook_Update(&Ghost->Look, Screen, State, time.PassedTime);
    return UIElementText_Refresh(&Ghost->Text, &Ghost->Base, &Ghost->Content);
}

/* The dragged item beside the pointer at the theme's offset, kept on the surface; the target's box clipped by every ancestor that cuts it. */
static Error DragGhost_Layout(void* self, const UILayoutContext* context)
{
    (void)context;
    DragGhost* Ghost = self;
    UIScreen* Screen = Widget_GetScreen(&Ghost->Base);
    UIDragSession Session;
    Ghost->HasTargetBox = false;
    if (!UIDragDrop_GetSession(Screen, &Session))
    {
        return Error_CreateSuccess();
    }
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Ghost->Look, Screen);
    float Scale = Widget_GetEffectiveScale(&Ghost->Base);
    Vector2 Surface = UIScreen_GetViewport(Screen).SizePixels;
    Vector2 Padding = UIThemeStyle_GetVector(Style, UIElementKind_Drag, UIDragPart_Padding);
    Vector2 Offset = UIThemeStyle_GetVector(Style, UIElementKind_Drag, UIDragPart_Offset);
    float MaxWidth = UIThemeStyle_GetNumber(Style, UIElementKind_Drag, UIDragPart_MaxWidth) * Scale;
    Vector2 Text = UIElementText_GetPixelSize(&Ghost->Text, Screen);
    float Width = RoundToPixel(Math_MinFloat(Text.x + (2.0f * Padding.x * Scale), Math_MaxFloat(MaxWidth, 2.0f * Padding.x * Scale)));
    float Height = RoundToPixel(Text.y + (2.0f * Padding.y * Scale));
    float X = RoundToPixel((Session.ScreenPosition.x * Surface.x) + (Offset.x * Scale));
    float Y = RoundToPixel((Session.ScreenPosition.y * Surface.y) + (Offset.y * Scale));
    X = Math_MaxFloat(0.0f, Math_MinFloat(X, Surface.x - Width));
    Y = Math_MaxFloat(0.0f, Math_MinFloat(Y, Surface.y - Height));
    Ghost->GhostBox = (Rectangle){ .x = X, .y = Y, .width = Width, .height = Height };
    Widget* Target = NULL;
    if ((Session.TargetState == UIDragTargetState_Accepting) && UIScreen_TryGetWidgetById(Screen, Session.TargetId, &Target))
    {
        Ghost->TargetBox = UIScreen_GetWidgetPixelBox(Screen, Target);
        Ghost->TargetClip = (Rectangle){ .x = 0.0f, .y = 0.0f, .width = Surface.x, .height = Surface.y };
        for (Widget* Above = Widget_GetParent(Target); Above != NULL; Above = Widget_GetParent(Above))
        {
            if (Widget_AreSubWidgetsCut(Above))
            {
                Ghost->TargetClip = Intersect(Ghost->TargetClip, UIScreen_GetWidgetPixelBox(Screen, Above));
            }
        }
        Ghost->HasTargetBox = !UIElement_IsBoxEmpty(Intersect(Ghost->TargetBox, Ghost->TargetClip));
    }
    return Error_CreateSuccess();
}

static Error DragGhost_Render(void* self, UIRenderContext* context)
{
    DragGhost* Ghost = self;
    if (!Ghost->IsShowing)
    {
        return Error_CreateSuccess();
    }
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Ghost->Look, Widget_GetScreen(&Ghost->Base));
    float Scale = Widget_GetEffectiveScale(&Ghost->Base);
    RenderContext* Render = UIRenderContext_GetRenderContext(context);
    Error Result = Error_CreateSuccess();
    if (Ghost->HasTargetBox)
    {
        RenderContext_PushClip(Render, Ghost->TargetClip);
        Result = UIElementLook_DrawBox(&Ghost->Look, Style, UIDragPart_Target, context, Ghost->TargetBox, Scale);
        RenderContext_PopClip(Render);
    }
    if ((Result.Code != ErrorCode_Success) || UIElement_IsBoxEmpty(Ghost->GhostBox))
    {
        return Result;
    }
    Result = UIElementLook_DrawBox(&Ghost->Look, Style, UIDragPart_Ghost, context, Ghost->GhostBox, Scale);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    const UIScreen* Screen = Widget_GetScreen(&Ghost->Base);
    Vector2 Text = UIElementText_GetPixelSize(&Ghost->Text, Screen);
    float Padding = RoundToPixel(UIThemeStyle_GetVector(Style, UIElementKind_Drag, UIDragPart_Padding).x * Scale);
    Color TextColor = UIElementLook_BlendColors(&Ghost->Look, UIThemeStyle_GetColors(Style, UIDragPart_Text), WHITE);
    RenderContext_PushClip(Render, Ghost->GhostBox);
    Result = UIElementText_Draw(&Ghost->Text, context, Ghost->GhostBox.x + Padding,
        Ghost->GhostBox.y + RoundToPixel((Ghost->GhostBox.height - Text.y) * 0.5f), TextColor);
    RenderContext_PopClip(Render);
    return Result;
}

static void* DragGhost_ResolveLook(void* widget)
{
    return &((DragGhost*)widget)->Look;
}


// Fields.
static const UILookVTable ListBoxLook =
{
    .Construct = ListBox_Construct,
    .Update = ListBox_Update,
    .Render = ListBox_Render,
    .Measure = ListBox_Measure,
    .GetThemeLook = ListBox_GetThemeLook
};

static const UILookVTable TreeBoxLook =
{
    .Construct = TreeBox_Construct,
    .Update = TreeBox_Update,
    .Render = TreeBox_Render,
    .Measure = TreeBox_Measure,
    .GetThemeLook = TreeBox_GetThemeLook
};

static const UILookVTable DialogBoxLook =
{
    .Construct = DialogBox_Construct,
    .Update = DialogBox_Update,
    .Render = DialogBox_Render,
    .Measure = DialogBox_Measure,
    .GetThemeLook = DialogBox_GetThemeLook
};

static const UILookVTable ColorPickerBoxLook =
{
    .Construct = ColorPickerBox_Construct,
    .Update = ColorPickerBox_Update,
    .Render = ColorPickerBox_Render,
    .Measure = ColorPickerBox_Measure,
    .GetThemeLook = ColorPickerBox_GetThemeLook
};

static const UILookVTable ToolbarBoxLook =
{
    .Construct = ToolbarBox_Construct,
    .Update = ToolbarBox_Update,
    .Layout = ToolbarBox_Layout,
    .Render = ToolbarBox_Render,
    .Measure = ToolbarBox_Measure,
    .GetThemeLook = ToolbarBox_GetThemeLook
};

static const UILookVTable WindowBoxLook =
{
    .Construct = WindowBox_Construct,
    .Deconstruct = WindowBox_Deconstruct,
    .Update = WindowBox_Update,
    .Render = WindowBox_Render,
    .Measure = WindowBox_Measure,
    .GetThemeLook = WindowBox_GetThemeLook
};

static const UILookVTable WorkspaceBoxLook =
{
    .Construct = WorkspaceBox_Construct,
    .Update = WorkspaceBox_Update,
    .Render = WorkspaceBox_Render,
    .Measure = WorkspaceBox_Measure,
    .GetThemeLook = WorkspaceBox_GetThemeLook
};

static const UILookVTable RenderViewBoxLook =
{
    .Construct = RenderViewBox_Construct,
    .Update = RenderViewBox_Update,
    .Render = RenderViewBox_Render,
    .Measure = RenderViewBox_Measure,
    .GetThemeLook = RenderViewBox_GetThemeLook
};

static const WidgetVTable DragGhostVTable =
{
    .OnDeconstruct = DragGhost_OnDeconstruct,
    .Update = DragGhost_Update,
    .Layout = DragGhost_Layout,
    .Render = DragGhost_Render
};


// Internal functions (declared in UIStandardLooksInternal.h).
Error UIStandardEditorLooks_Register(UIKit* kit)
{
    Error Result = UIKit_RegisterLook(kit, UIElementKind_List, UI_KIT_STANDARD_LOOK, sizeof(ListBox), &ListBoxLook, NULL);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIKit_RegisterLook(kit, UIElementKind_Tree, UI_KIT_STANDARD_LOOK, sizeof(TreeBox), &TreeBoxLook, NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIKit_RegisterLook(kit, UIElementKind_Dialog, UI_KIT_STANDARD_LOOK, sizeof(DialogBox), &DialogBoxLook, NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIKit_RegisterLook(kit, UIElementKind_ColorPicker, UI_KIT_STANDARD_LOOK, sizeof(ColorPickerBox), &ColorPickerBoxLook, NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIKit_RegisterLook(kit, UIElementKind_Toolbar, UI_KIT_STANDARD_LOOK, sizeof(ToolbarBox), &ToolbarBoxLook, NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIKit_RegisterLook(kit, UIElementKind_Window, UI_KIT_STANDARD_LOOK, sizeof(WindowBox), &WindowBoxLook, NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIKit_RegisterLook(kit, UIElementKind_Workspace, UI_KIT_STANDARD_LOOK, sizeof(WorkspaceBox), &WorkspaceBoxLook, NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIKit_RegisterLook(kit, UIElementKind_RenderView, UI_KIT_STANDARD_LOOK, sizeof(RenderViewBox), &RenderViewBoxLook, NULL);
    }
    return Result;
}


// Public functions (declared in UIStandardLooks.h).
Error UIStandardLooks_RegisterDragGhost(UIKit* kit, UIWidgetFactory* factory, uint64_t* outTypeId)
{
    WidgetCapabilityEntry Capabilities[] =
    {
        { .CapabilityId = UIKit_GetCapabilityId(kit, UIElementCapability_Look), .Resolver = DragGhost_ResolveLook }
    };
    WidgetTypeDescriptor Descriptor =
    {
        .Name = u8"DragGhost",
        .StructSize = sizeof(DragGhost),
        .VTable = &DragGhostVTable,
        .Constructor = DragGhost_Construct,
        .Capabilities = Capabilities,
        .CapabilityCount = sizeof(Capabilities) / sizeof(Capabilities[0]),
        .Properties = NULL,
        .PropertyCount = 0U,
        .IsFocusable = false
    };
    return UIWidgetFactory_RegisterType(factory, &Descriptor, outTypeId);
}
