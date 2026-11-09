#include "UIStandardLooks.h"
#include "UIStandardLooksInternal.h"
#include "UITypingView.h"
#include "TextComponentFactory.h"
#include "UIKit.h"
#include "UIScreen.h"
#include "UITooltip.h"
#include "UIWidgetFactory.h"
#include "wr/WRMath.h"


// Macros.
/** How far a toggle's check stands inside its box, in UI pixels, and how much of the box's height a mixed state's bar takes. */
#define CHECK_INSET 3.0f
#define MIXED_BAR_SHARE 0.25f
/** The custom value of a button's theme block for the gap between its icon and its text, in UI pixels, and its default. */
#define ICON_SPACING_KEY ((const unsigned char*)u8"icon_spacing")
#define DEFAULT_ICON_SPACING 4.0f


// Types.
/* The standard panel: a background from the theme, its children inside the theme's padding. */
typedef struct PanelBoxStruct
{
    UIPanelLookBase Base;
    UIElementLook Look;
} PanelBox;

/* The standard button: a background, an icon and a line of text, centred. */
typedef struct ButtonBoxStruct
{
    UIButtonLookBase Base;
    UIElementLook Look;
    UIElementText Text;
    UIElementIconView Icon;
} ButtonBox;

/* The standard toggle: a box at the left with its check, and the text after it. */
typedef struct ToggleBoxStruct
{
    UIToggleLookBase Base;
    UIElementLook Look;
    UIElementText Text;
} ToggleBox;

/* The standard slider: a track across the box, the fill up to the thumb and the thumb. */
typedef struct SliderBoxStruct
{
    UISliderLookBase Base;
    UIElementLook Look;
} SliderBox;

/* The standard scroll bar: the track over the whole bar, the thumb inside it. The thumb's state is its own (pressed while dragged,
 * hovered under the pointer), so it fades on a look of its own. */
typedef struct ScrollbarBoxStruct
{
    UIScrollbarLookBase Base;
    UIElementLook Look;
    UIElementLook ThumbLook;
} ScrollbarBox;

/* The standard scroll area: a background, and the corner where both bars meet. */
typedef struct ScrollAreaBoxStruct
{
    UIScrollAreaLookBase Base;
    UIElementLook Look;
} ScrollAreaBox;

/* The standard dropdown head: a background, the text at the left and the arrow at the right. */
typedef struct DropdownBoxStruct
{
    UIDropdownLookBase Base;
    UIElementLook Look;
    UIElementText Text;
} DropdownBox;

/* The standard menu bar: a background, its entries in a row inside the theme's padding. */
typedef struct MenuBarBoxStruct
{
    UIMenuBarLookBase Base;
    UIElementLook Look;
} MenuBarBox;

/* The standard status bar: a background, its sections inside the theme's padding, and its text at the start of the centre section. */
typedef struct StatusBarBoxStruct
{
    UIStatusBarLookBase Base;
    UIElementLook Look;
    UIElementText Text;
} StatusBarBox;

/* The standard text box: a box (a read-only box's of its own) and, inside its padding, the text in its view, the selection behind it, the
 * placeholder while it is empty and a blinking caret while it is being edited (UITypingView). */
typedef struct TextBoxBoxStruct
{
    UITextBoxLookBase Base;
    UIElementLook Look;
    UITypingView Typing;
    UIElementText Placeholder;
} TextBoxBox;

/* What lies under a modal popup: the theme's backdrop over the whole surface. A plain widget type, made by the screen (UIPopup.h) with
 * no arguments; it takes input, so presses, the wheel and hover stop at it. */
typedef struct BackdropBoxStruct
{
    Widget Base;
    UIElementLook Look;
} BackdropBox;

/* What shows hover text: a box beside the pointer with the text in it, wrapped at the theme's width. A plain widget type, made by
 * the screen (UITooltip.h) with no arguments. */
typedef struct TooltipBoxStruct
{
    Widget Base;
    UIElementLook Look;
    UIElementText Text;
    UITextContent Content;
    bool IsShowing;
} TooltipBox;


// Static functions: shared.
static float RoundToPixel(float value)
{
    return Math_FloorFloat(value + 0.5f);
}

/* Keeps the first error and releases a later one. */
static Error KeepFirst(Error first, Error later)
{
    if (first.Code == ErrorCode_Success)
    {
        return later;
    }
    Error_Deconstruct(&later);
    return first;
}


// Internal functions (declared in UIStandardLooksInternal.h).
UIInsets UIStandardLooks_GetBorderInsets(const UIBoxStyle* style)
{
    if ((style != NULL) && (style->Kind == UIBoxKind_NineSlice) && (style->Texture != NULL))
    {
        float PixelScale = ((style->PixelScale > 0.0f) && !Math_IsInfinityFloat(style->PixelScale)) ? style->PixelScale : 1.0f;
        return (UIInsets){ .Left = style->Margins.Left * PixelScale, .Top = style->Margins.Top * PixelScale,
            .Right = style->Margins.Right * PixelScale, .Bottom = style->Margins.Bottom * PixelScale };
    }
    if ((style != NULL) && (style->Kind == UIBoxKind_Flat) && style->HasOutline)
    {
        return UIInsets_All(style->OutlineThickness);
    }
    return UIInsets_All(0.0f);
}


// Static functions: the panel.
static Error PanelBox_Construct(void* self, void* args)
{
    const UIPanelOptions* Options = args;
    return UIElementLook_Construct(&((PanelBox*)self)->Look, UIElementKind_Panel, Options->Element.Variant);
}

static Error PanelBox_Update(void* self, ProgramTime time)
{
    PanelBox* Box = self;
    UIElementLook_Update(&Box->Look, Widget_GetScreen(&Box->Base.Base), UIElementState_Normal, time.PassedTime);
    return Error_CreateSuccess();
}

/* How the panel stacks its children: the options' padding and spacing, else the theme's. */
static UIStackLayout PanelBox_GetLayout(const PanelBox* box)
{
    const UIThemeStyle* Style = UIElementLook_GetStyle(&box->Look, Widget_GetScreen(&box->Base.Base));
    Vector2 Padding = UIThemeStyle_GetVector(Style, UIElementKind_Panel, UIPanelPart_Padding);
    UIInsets Insets = { .Left = Padding.x, .Top = Padding.y, .Right = Padding.x, .Bottom = Padding.y };
    return UIPanel_GetLayout(&box->Base.Panel, Insets, UIThemeStyle_GetNumber(Style, UIElementKind_Panel, UIPanelPart_Spacing));
}

static Error PanelBox_Layout(void* self, const UILayoutContext* context)
{
    PanelBox* Box = self;
    UIStackLayout Layout = PanelBox_GetLayout(Box);
    return UIStackLayout_Arrange(&Layout, &Box->Base.Base, context);
}

static Error PanelBox_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    PanelBox* Box = self;
    UIStackLayout Layout = PanelBox_GetLayout(Box);
    Error Result = UIStackLayout_Measure(&Layout, &Box->Base.Base, context, availableWidth, outSize);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Vector2 Minimum = UIThemeStyle_GetVector(UIElementLook_GetStyle(&Box->Look, Widget_GetScreen(&Box->Base.Base)), UIElementKind_Panel,
        UIPanelPart_MinSize);
    outSize->x = Math_MaxFloat(outSize->x, Minimum.x);
    outSize->y = Math_MaxFloat(outSize->y, Minimum.y);
    return Error_CreateSuccess();
}

static Error PanelBox_Render(void* self, UIRenderContext* context)
{
    PanelBox* Box = self;
    Rectangle Pixels = UIRenderContext_GetPixelBox(context);
    if (UIElement_IsBoxEmpty(Pixels))
    {
        return Error_CreateSuccess();
    }
    return UIElementLook_DrawBox(&Box->Look, UIElementLook_GetStyle(&Box->Look, Widget_GetScreen(&Box->Base.Base)), UIPanelPart_Background,
        context, Pixels, Widget_GetEffectiveScale(&Box->Base.Base));
}

static UIElementLook* PanelBox_GetThemeLook(void* self)
{
    return &((PanelBox*)self)->Look;
}


// Static functions: the button.
static Error ButtonBox_Construct(void* self, void* args)
{
    ButtonBox* Box = self;
    const UIButtonArgs* Arguments = args;
    Error Result = UIElementLook_Construct(&Box->Look, UIElementKind_Button, Arguments->Options->Element.Variant);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = UIElementText_Construct(&Box->Text, UIKit_GetTextFactory(Arguments->Kit), UIKit_GetAssets(Arguments->Kit),
        UIKit_GetLogger(Arguments->Kit));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = UIElementIconView_Construct(&Box->Icon, UIKit_GetTextFactory(Arguments->Kit), UIKit_GetAssets(Arguments->Kit),
        UIKit_GetLogger(Arguments->Kit));
    if (Result.Code != ErrorCode_Success)
    {
        Error Cleanup = UIElementText_Deconstruct(&Box->Text);
        Error_Deconstruct(&Cleanup);
    }
    return Result;
}

static Error ButtonBox_Deconstruct(void* self)
{
    ButtonBox* Box = self;
    return KeepFirst(UIElementIconView_Deconstruct(&Box->Icon), UIElementText_Deconstruct(&Box->Text));
}

/* Brings the text and the icon in line with the capability's content. */
static Error ButtonBox_Refresh(ButtonBox* box)
{
    Error Result = UIElementText_Refresh(&box->Text, &box->Base.Base, &box->Base.Button._text);
    return (Result.Code == ErrorCode_Success) ? UIElementIconView_Refresh(&box->Icon, &box->Base.Base, &box->Base.Button._icon) : Result;
}

static Error ButtonBox_Update(void* self, ProgramTime time)
{
    ButtonBox* Box = self;
    UIElementLook_Update(&Box->Look, Widget_GetScreen(&Box->Base.Base), UIButtonCapability_GetState(&Box->Base.Button, &Box->Base.Base),
        time.PassedTime);
    Error Result = ButtonBox_Refresh(Box);
    return (Result.Code == ErrorCode_Success) ? UIElementIconView_Update(&Box->Icon, time) : Result;
}

/* The gap between the icon and the text in surface pixels: none without both. */
static float ButtonBox_GetGapPixels(const ButtonBox* box, const UIThemeStyle* style, Vector2 text)
{
    if (!UIElementIconView_IsShowing(&box->Icon) || !(text.x > 0.0f))
    {
        return 0.0f;
    }
    return RoundToPixel(UIThemeStyle_FindCustomNumber(style, ICON_SPACING_KEY, DEFAULT_ICON_SPACING) * Widget_GetEffectiveScale(&box->Base.Base));
}

static Error ButtonBox_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)context;
    (void)availableWidth;
    ButtonBox* Box = self;
    Error Result = ButtonBox_Refresh(Box);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    const UIScreen* Screen = Widget_GetScreen(&Box->Base.Base);
    float Scale = Widget_GetEffectiveScale(&Box->Base.Base);
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Box->Look, Screen);
    Vector2 Text = UIElementText_GetPixelSize(&Box->Text, Screen);
    Vector2 Icon = UIElementIconView_GetPixelSize(&Box->Icon, Screen);
    float ContentWidth = Icon.x + ButtonBox_GetGapPixels(Box, Style, Text) + Text.x;
    float ContentHeight = Math_MaxFloat(Icon.y, Text.y);
    Vector2 Padding = UIThemeStyle_GetVector(Style, UIElementKind_Button, UIButtonPart_Padding);
    Vector2 Minimum = UIThemeStyle_GetVector(Style, UIElementKind_Button, UIButtonPart_MinSize);
    outSize->x = Math_MaxFloat((ContentWidth / Scale) + (2.0f * Padding.x), Minimum.x);
    outSize->y = Math_MaxFloat((ContentHeight / Scale) + (2.0f * Padding.y), Minimum.y);
    return Error_CreateSuccess();
}

/* The icon and the text, centred in the box as one (from the padding and clipped to the box when too long). */
static Error ButtonBox_RenderContent(const ButtonBox* box, UIRenderContext* context, Rectangle pixels)
{
    const UIScreen* Screen = Widget_GetScreen(&box->Base.Base);
    const UIThemeStyle* Style = UIElementLook_GetStyle(&box->Look, Screen);
    Color TextColor = UIElementLook_BlendColors(&box->Look, UIThemeStyle_GetColors(Style, UIButtonPart_Text), WHITE);
    Vector2 Text = UIElementText_GetPixelSize(&box->Text, Screen);
    Vector2 Icon = UIElementIconView_GetPixelSize(&box->Icon, Screen);
    float Gap = ButtonBox_GetGapPixels(box, Style, Text);
    float ContentWidth = Icon.x + Gap + Text.x;
    bool IsTooLong = ContentWidth > pixels.width;
    float PaddingPixels = UIThemeStyle_GetVector(Style, UIElementKind_Button, UIButtonPart_Padding).x * Widget_GetEffectiveScale(&box->Base.Base);
    float X = IsTooLong ? (pixels.x + PaddingPixels) : (pixels.x + RoundToPixel((pixels.width - ContentWidth) * 0.5f));
    if (IsTooLong)
    {
        RenderContext_PushClip(UIRenderContext_GetRenderContext(context), pixels);
    }
    Error Result = UIElementIconView_Draw(&box->Icon, context, X, pixels.y + RoundToPixel((pixels.height - Icon.y) * 0.5f), TextColor);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIElementText_Draw(&box->Text, context, X + Icon.x + Gap, pixels.y + RoundToPixel((pixels.height - Text.y) * 0.5f), TextColor);
    }
    if (IsTooLong)
    {
        RenderContext_PopClip(UIRenderContext_GetRenderContext(context));
    }
    return Result;
}

static Error ButtonBox_Render(void* self, UIRenderContext* context)
{
    ButtonBox* Box = self;
    Rectangle Pixels = UIRenderContext_GetPixelBox(context);
    if (UIElement_IsBoxEmpty(Pixels))
    {
        return Error_CreateSuccess();
    }
    Error Result = UIElementLook_DrawBox(&Box->Look, UIElementLook_GetStyle(&Box->Look, Widget_GetScreen(&Box->Base.Base)),
        UIButtonPart_Background, context, Pixels, Widget_GetEffectiveScale(&Box->Base.Base));
    return (Result.Code == ErrorCode_Success) ? ButtonBox_RenderContent(Box, context, Pixels) : Result;
}

static UIElementLook* ButtonBox_GetThemeLook(void* self)
{
    return &((ButtonBox*)self)->Look;
}


// Static functions: the toggle.
static Error ToggleBox_Construct(void* self, void* args)
{
    ToggleBox* Box = self;
    const UIToggleArgs* Arguments = args;
    Error Result = UIElementLook_Construct(&Box->Look, UIElementKind_Toggle, Arguments->Options->Element.Variant);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return UIElementText_Construct(&Box->Text, UIKit_GetTextFactory(Arguments->Kit), UIKit_GetAssets(Arguments->Kit),
        UIKit_GetLogger(Arguments->Kit));
}

static Error ToggleBox_Deconstruct(void* self)
{
    return UIElementText_Deconstruct(&((ToggleBox*)self)->Text);
}

static Error ToggleBox_Update(void* self, ProgramTime time)
{
    ToggleBox* Box = self;
    UIElementLook_Update(&Box->Look, Widget_GetScreen(&Box->Base.Base), UIToggleCapability_GetState(&Box->Base.Toggle, &Box->Base.Base),
        time.PassedTime);
    return UIElementText_Refresh(&Box->Text, &Box->Base.Base, &Box->Base.Toggle._text);
}

static Error ToggleBox_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)context;
    (void)availableWidth;
    ToggleBox* Box = self;
    Error Result = UIElementText_Refresh(&Box->Text, &Box->Base.Base, &Box->Base.Toggle._text);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    const UIScreen* Screen = Widget_GetScreen(&Box->Base.Base);
    float Scale = Widget_GetEffectiveScale(&Box->Base.Base);
    Vector2 Text = UIElementText_GetPixelSize(&Box->Text, Screen);
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Box->Look, Screen);
    Vector2 BoxSize = UIThemeStyle_GetVector(Style, UIElementKind_Toggle, UITogglePart_BoxSize);
    Vector2 Minimum = UIThemeStyle_GetVector(Style, UIElementKind_Toggle, UITogglePart_MinSize);
    float Spacing = UIThemeStyle_GetNumber(Style, UIElementKind_Toggle, UITogglePart_Spacing);
    outSize->x = Math_MaxFloat(BoxSize.x + Spacing + (Text.x / Scale), Minimum.x);
    outSize->y = Math_MaxFloat(Math_MaxFloat(BoxSize.y, Text.y / Scale), Minimum.y);
    return Error_CreateSuccess();
}

static Error ToggleBox_Render(void* self, UIRenderContext* context)
{
    ToggleBox* Box = self;
    const UIScreen* Screen = Widget_GetScreen(&Box->Base.Base);
    float Scale = Widget_GetEffectiveScale(&Box->Base.Base);
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Box->Look, Screen);
    Rectangle Whole = UIRenderContext_GetPixelBox(context);
    if (UIElement_IsBoxEmpty(Whole))
    {
        return Error_CreateSuccess();
    }
    Vector2 BoxSize = UIThemeStyle_GetVector(Style, UIElementKind_Toggle, UITogglePart_BoxSize);
    Rectangle Check =
    {
        .x = Whole.x,
        .y = Whole.y + RoundToPixel((Whole.height - (BoxSize.y * Scale)) * 0.5f),
        .width = RoundToPixel(BoxSize.x * Scale),
        .height = RoundToPixel(BoxSize.y * Scale)
    };
    Error Result = UIElementLook_DrawBox(&Box->Look, Style, UITogglePart_Box, context, Check, Scale);
    UIToggleState State = Box->Base.Toggle._state;
    if ((Result.Code == ErrorCode_Success) && (State != UIToggleState_Off))
    {
        float Inset = RoundToPixel(CHECK_INSET * Scale);
        Rectangle Mark = { .x = Check.x + Inset, .y = Check.y + Inset, .width = Math_MaxFloat(0.0f, Check.width - (2.0f * Inset)),
            .height = Math_MaxFloat(0.0f, Check.height - (2.0f * Inset)) };
        if (State == UIToggleState_Mixed)
        {
            float Bar = Math_MaxFloat(1.0f, RoundToPixel(Check.height * MIXED_BAR_SHARE));
            Mark.y = Check.y + RoundToPixel((Check.height - Bar) * 0.5f);
            Mark.height = Bar;
        }
        Result = UIElementLook_DrawBox(&Box->Look, Style, UITogglePart_Check, context, Mark, Scale);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Vector2 Text = UIElementText_GetPixelSize(&Box->Text, Screen);
    float TextX = Check.x + Check.width + RoundToPixel(UIThemeStyle_GetNumber(Style, UIElementKind_Toggle, UITogglePart_Spacing) * Scale);
    float TextY = Whole.y + RoundToPixel((Whole.height - Text.y) * 0.5f);
    Color TextColor = UIElementLook_BlendColors(&Box->Look, UIThemeStyle_GetColors(Style, UITogglePart_Text), WHITE);
    return UIElementText_Draw(&Box->Text, context, TextX, TextY, TextColor);
}

static UIElementLook* ToggleBox_GetThemeLook(void* self)
{
    return &((ToggleBox*)self)->Look;
}


// Static functions: the slider.
static Error SliderBox_Construct(void* self, void* args)
{
    const UISliderArgs* Arguments = args;
    return UIElementLook_Construct(&((SliderBox*)self)->Look, UIElementKind_Slider, Arguments->Options->Element.Variant);
}

static Error SliderBox_Update(void* self, ProgramTime time)
{
    SliderBox* Box = self;
    const UIScreen* Screen = Widget_GetScreen(&Box->Base.Base);
    UIElementLook_Update(&Box->Look, (UIScreen*)Screen, UISliderCapability_GetState(&Box->Base.Slider, &Box->Base.Base), time.PassedTime);
    Vector2 Thumb = UIThemeStyle_GetVector(UIElementLook_GetStyle(&Box->Look, Screen), UIElementKind_Slider, UISliderPart_ThumbSize);
    UISliderCapability_SetThumbLength(&Box->Base.Slider, Thumb.x);
    return Error_CreateSuccess();
}

/* The slider's sizes as the theme gives them for a track across: along the track first, across it second. Upright, they swap axes. */
static Vector2 SliderBox_Orient(const SliderBox* box, Vector2 acrossSize)
{
    bool IsVertical = UISliderCapability_GetOrientation(&box->Base.Slider) == UISliderOrientation_Vertical;
    return IsVertical ? (Vector2){ .x = acrossSize.y, .y = acrossSize.x } : acrossSize;
}

static Error SliderBox_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)context;
    (void)availableWidth;
    SliderBox* Box = self;
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Box->Look, Widget_GetScreen(&Box->Base.Base));
    Vector2 Minimum = UIThemeStyle_GetVector(Style, UIElementKind_Slider, UISliderPart_MinSize);
    Vector2 Thumb = UIThemeStyle_GetVector(Style, UIElementKind_Slider, UISliderPart_ThumbSize);
    *outSize = SliderBox_Orient(Box, (Vector2){ .x = Math_MaxFloat(Minimum.x, Thumb.x), .y = Math_MaxFloat(Minimum.y, Thumb.y) });
    return Error_CreateSuccess();
}

static Error SliderBox_Render(void* self, UIRenderContext* context)
{
    SliderBox* Box = self;
    const UISliderCapability* Slider = &Box->Base.Slider;
    float Scale = Widget_GetEffectiveScale(&Box->Base.Base);
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Box->Look, Widget_GetScreen(&Box->Base.Base));
    Rectangle Pixels = UIRenderContext_GetPixelBox(context);
    if (UIElement_IsBoxEmpty(Pixels))
    {
        return Error_CreateSuccess();
    }

    bool IsVertical = UISliderCapability_GetOrientation(Slider) == UISliderOrientation_Vertical;
    float Thickness = Math_MaxFloat(1.0f, RoundToPixel(UIThemeStyle_GetNumber(Style, UIElementKind_Slider, UISliderPart_TrackThickness) * Scale));
    Vector2 ThumbSize = UIThemeStyle_GetVector(Style, UIElementKind_Slider, UISliderPart_ThumbSize);
    float ThumbAlong = RoundToPixel(ThumbSize.x * Scale);
    float ThumbAcross = RoundToPixel(ThumbSize.y * Scale);
    float Share = (float)UISliderCapability_GetShare(Slider);
    float Length = IsVertical ? Pixels.height : Pixels.width;
    float Breadth = IsVertical ? Pixels.width : Pixels.height;
    // Along the track from its low end (the left, or the bottom when upright), then turned onto the box.
    float ThumbStart = RoundToPixel(Share * Math_MaxFloat(0.0f, Length - ThumbAlong));
    float FillLength = ThumbStart + (ThumbAlong * 0.5f);
    float TrackOffset = RoundToPixel((Breadth - Thickness) * 0.5f);
    float ThumbOffset = RoundToPixel((Breadth - ThumbAcross) * 0.5f);
    Rectangle Track = { 0 };
    Rectangle Fill = { 0 };
    Rectangle Thumb = { 0 };
    if (IsVertical)
    {
        float Bottom = Pixels.y + Pixels.height;
        Track = (Rectangle){ .x = Pixels.x + TrackOffset, .y = Pixels.y, .width = Thickness, .height = Pixels.height };
        Fill = (Rectangle){ .x = Track.x, .y = Bottom - FillLength, .width = Thickness, .height = FillLength };
        Thumb = (Rectangle){ .x = Pixels.x + ThumbOffset, .y = Bottom - ThumbStart - ThumbAlong, .width = ThumbAcross, .height = ThumbAlong };
    }
    else
    {
        Track = (Rectangle){ .x = Pixels.x, .y = Pixels.y + TrackOffset, .width = Pixels.width, .height = Thickness };
        Fill = (Rectangle){ .x = Pixels.x, .y = Track.y, .width = FillLength, .height = Thickness };
        Thumb = (Rectangle){ .x = Pixels.x + ThumbStart, .y = Pixels.y + ThumbOffset, .width = ThumbAlong, .height = ThumbAcross };
    }
    Error Result = UIElementLook_DrawBox(&Box->Look, Style, UISliderPart_Track, context, Track, Scale);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIElementLook_DrawBox(&Box->Look, Style, UISliderPart_Fill, context, Fill, Scale);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIElementLook_DrawBox(&Box->Look, Style, UISliderPart_Thumb, context, Thumb, Scale);
    }
    return Result;
}

static UIElementLook* SliderBox_GetThemeLook(void* self)
{
    return &((SliderBox*)self)->Look;
}


// Static functions: the scroll bar.
static Error ScrollbarBox_Construct(void* self, void* args)
{
    ScrollbarBox* Box = self;
    const UIScrollbarArgs* Arguments = args;
    Error Result = UIElementLook_Construct(&Box->Look, UIElementKind_Scrollbar, Arguments->Options->Element.Variant);
    return (Result.Code == ErrorCode_Success) ? UIElementLook_Construct(&Box->ThumbLook, UIElementKind_Scrollbar, Arguments->Options->Element.Variant)
        : Result;
}

static Error ScrollbarBox_Update(void* self, ProgramTime time)
{
    ScrollbarBox* Box = self;
    UIScreen* Screen = Widget_GetScreen(&Box->Base.Base);
    // A variant set on the bar (through its theme look) is the thumb's too.
    if (UIElementLook_SetVariant(&Box->ThumbLook, Box->Look._variant).Code != ErrorCode_Success)
    {
        Error Cleanup = UIElementLook_SetVariant(&Box->ThumbLook, NULL);
        Error_Deconstruct(&Cleanup);
    }
    UIElementState TrackState = Widget_IsHovered(&Box->Base.Base) ? UIElementState_Hovered : UIElementState_Normal;
    UIElementLook_Update(&Box->Look, Screen, TrackState, time.PassedTime);
    UIElementLook_Update(&Box->ThumbLook, Screen, UIScrollbarCapability_GetThumbState(&Box->Base.Scrollbar), time.PassedTime);
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Box->Look, Screen);
    UIScrollbarCapability_SetMinThumbLength(&Box->Base.Scrollbar,
        UIThemeStyle_GetNumber(Style, UIElementKind_Scrollbar, UIScrollbarPart_MinThumbLength));
    return Error_CreateSuccess();
}

static Error ScrollbarBox_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)context;
    (void)availableWidth;
    ScrollbarBox* Box = self;
    float Thickness = UIThemeStyle_GetNumber(UIElementLook_GetStyle(&Box->Look, Widget_GetScreen(&Box->Base.Base)), UIElementKind_Scrollbar,
        UIScrollbarPart_Thickness);
    *outSize = (Vector2){ .x = Thickness, .y = Thickness };
    return Error_CreateSuccess();
}

static Error ScrollbarBox_Render(void* self, UIRenderContext* context)
{
    ScrollbarBox* Box = self;
    Rectangle Pixels = UIRenderContext_GetPixelBox(context);
    if (UIElement_IsBoxEmpty(Pixels))
    {
        return Error_CreateSuccess();
    }
    float Scale = Widget_GetEffectiveScale(&Box->Base.Base);
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Box->Look, Widget_GetScreen(&Box->Base.Base));
    Error Result = UIElementLook_DrawBox(&Box->Look, Style, UIScrollbarPart_Track, context, Pixels, Scale);
    Rectangle Thumb;
    float Inset = RoundToPixel(UIThemeStyle_GetNumber(Style, UIElementKind_Scrollbar, UIScrollbarPart_ThumbInset) * Scale);
    if ((Result.Code != ErrorCode_Success) || !UIScrollbarCapability_GetThumbBox(&Box->Base.Scrollbar, &Box->Base.Base, Pixels, Inset, &Thumb)
        || UIElement_IsBoxEmpty(Thumb))
    {
        return Result; // Nothing to scroll: the track alone.
    }
    return UIElementLook_DrawBox(&Box->ThumbLook, Style, UIScrollbarPart_Thumb, context, Thumb, Scale);
}

static UIElementLook* ScrollbarBox_GetThemeLook(void* self)
{
    return &((ScrollbarBox*)self)->Look;
}


// Static functions: the scroll area.
static Error ScrollAreaBox_Construct(void* self, void* args)
{
    const UIScrollAreaArgs* Arguments = args;
    return UIElementLook_Construct(&((ScrollAreaBox*)self)->Look, UIElementKind_ScrollArea, Arguments->Options->Element.Variant);
}

static Error ScrollAreaBox_Update(void* self, ProgramTime time)
{
    ScrollAreaBox* Box = self;
    UIElementLook_Update(&Box->Look, Widget_GetScreen(&Box->Base.Base), UIElementState_Normal, time.PassedTime);
    return Error_CreateSuccess();
}

static Error ScrollAreaBox_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    ScrollAreaBox* Box = self;
    return UIScrollArea_MeasurePreferred(&Box->Base.Area, &Box->Base.Base, context, availableWidth, outSize);
}

static Error ScrollAreaBox_Render(void* self, UIRenderContext* context)
{
    ScrollAreaBox* Box = self;
    Rectangle Pixels = UIRenderContext_GetPixelBox(context);
    if (UIElement_IsBoxEmpty(Pixels))
    {
        return Error_CreateSuccess();
    }
    return UIElementLook_DrawBox(&Box->Look, UIElementLook_GetStyle(&Box->Look, Widget_GetScreen(&Box->Base.Base)), UIScrollAreaPart_Background,
        context, Pixels, Widget_GetEffectiveScale(&Box->Base.Base));
}

static UIElementLook* ScrollAreaBox_GetThemeLook(void* self)
{
    return &((ScrollAreaBox*)self)->Look;
}


// Static functions: the dropdown's head.
static Error DropdownBox_Construct(void* self, void* args)
{
    DropdownBox* Box = self;
    const UIDropdownArgs* Arguments = args;
    Error Result = UIElementLook_Construct(&Box->Look, UIElementKind_Dropdown, Arguments->Options->Element.Variant);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return UIElementText_Construct(&Box->Text, UIKit_GetTextFactory(Arguments->Kit), UIKit_GetAssets(Arguments->Kit),
        UIKit_GetLogger(Arguments->Kit));
}

static Error DropdownBox_Deconstruct(void* self)
{
    return UIElementText_Deconstruct(&((DropdownBox*)self)->Text);
}

static Error DropdownBox_Update(void* self, ProgramTime time)
{
    DropdownBox* Box = self;
    UIElementLook_Update(&Box->Look, Widget_GetScreen(&Box->Base.Base), UIDropdownCapability_GetState(&Box->Base.Dropdown, &Box->Base.Base),
        time.PassedTime);
    return UIElementText_Refresh(&Box->Text, &Box->Base.Base, UIDropdownCapability_GetShownText(&Box->Base.Dropdown));
}

static Error DropdownBox_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)context;
    (void)availableWidth;
    DropdownBox* Box = self;
    Error Result = UIElementText_Refresh(&Box->Text, &Box->Base.Base, UIDropdownCapability_GetShownText(&Box->Base.Dropdown));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    const UIScreen* Screen = Widget_GetScreen(&Box->Base.Base);
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Box->Look, Screen);
    Vector2 Text = UIElementText_GetPixelSize(&Box->Text, Screen);
    float Scale = Widget_GetEffectiveScale(&Box->Base.Base);
    Vector2 Padding = UIThemeStyle_GetVector(Style, UIElementKind_Dropdown, UIDropdownPart_Padding);
    Vector2 Minimum = UIThemeStyle_GetVector(Style, UIElementKind_Dropdown, UIDropdownPart_MinSize);
    float Arrow = UIThemeStyle_GetNumber(Style, UIElementKind_Dropdown, UIDropdownPart_ArrowSize);
    // The text, a gap of the padding, the arrow, and the padding on both sides.
    outSize->x = Math_MaxFloat((Text.x / Scale) + (3.0f * Padding.x) + Arrow, Minimum.x);
    outSize->y = Math_MaxFloat((Text.y / Scale) + (2.0f * Padding.y), Minimum.y);
    return Error_CreateSuccess();
}

static Error DropdownBox_Render(void* self, UIRenderContext* context)
{
    DropdownBox* Box = self;
    Rectangle Pixels = UIRenderContext_GetPixelBox(context);
    if (UIElement_IsBoxEmpty(Pixels))
    {
        return Error_CreateSuccess();
    }
    const UIScreen* Screen = Widget_GetScreen(&Box->Base.Base);
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Box->Look, Screen);
    float Scale = Widget_GetEffectiveScale(&Box->Base.Base);
    Error Result = UIElementLook_DrawBox(&Box->Look, Style, UIDropdownPart_Background, context, Pixels, Scale);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    float Padding = RoundToPixel(UIThemeStyle_GetVector(Style, UIElementKind_Dropdown, UIDropdownPart_Padding).x * Scale);
    float Arrow = RoundToPixel(UIThemeStyle_GetNumber(Style, UIElementKind_Dropdown, UIDropdownPart_ArrowSize) * Scale);
    Rectangle ArrowBox = { .x = (Pixels.x + Pixels.width) - Padding - Arrow, .y = Pixels.y, .width = Arrow, .height = Pixels.height };
    UIElement_DrawArrow(context, ArrowBox, UIArrowDirection_Down,
        UIElementLook_BlendColors(&Box->Look, UIThemeStyle_GetColors(Style, UIDropdownPart_Arrow), WHITE));
    // The text from the padding, cut where the arrow's gap begins.
    Vector2 Text = UIElementText_GetPixelSize(&Box->Text, Screen);
    Rectangle TextArea = { .x = Pixels.x, .y = Pixels.y, .width = Math_MaxFloat(0.0f, ArrowBox.x - Padding - Pixels.x), .height = Pixels.height };
    Color TextColor = UIElementLook_BlendColors(&Box->Look, UIThemeStyle_GetColors(Style, UIDropdownPart_Text), WHITE);
    RenderContext_PushClip(UIRenderContext_GetRenderContext(context), TextArea);
    Result = UIElementText_Draw(&Box->Text, context, Pixels.x + Padding, Pixels.y + RoundToPixel((Pixels.height - Text.y) * 0.5f), TextColor);
    RenderContext_PopClip(UIRenderContext_GetRenderContext(context));
    return Result;
}

static UIElementLook* DropdownBox_GetThemeLook(void* self)
{
    return &((DropdownBox*)self)->Look;
}


// Static functions: the menu bar.
static Error MenuBarBox_Construct(void* self, void* args)
{
    const UIMenuBarArgs* Arguments = args;
    return UIElementLook_Construct(&((MenuBarBox*)self)->Look, UIElementKind_MenuBar, Arguments->Options->Element.Variant);
}

static Error MenuBarBox_Update(void* self, ProgramTime time)
{
    MenuBarBox* Box = self;
    UIElementLook_Update(&Box->Look, Widget_GetScreen(&Box->Base.Base), UIElementState_Normal, time.PassedTime);
    return Error_CreateSuccess();
}

static Vector2 MenuBarBox_GetPadding(const MenuBarBox* box)
{
    return UIThemeStyle_GetVector(UIElementLook_GetStyle(&box->Look, Widget_GetScreen(&box->Base.Base)), UIElementKind_MenuBar, UIMenuBarPart_Padding);
}

static Error MenuBarBox_Layout(void* self, const UILayoutContext* context)
{
    MenuBarBox* Box = self;
    return UIMenuBar_ArrangeEntries(&Box->Base.Base, context, MenuBarBox_GetPadding(Box));
}

static Error MenuBarBox_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)availableWidth;
    MenuBarBox* Box = self;
    Error Result = UIMenuBar_MeasureEntries(&Box->Base.Base, context, MenuBarBox_GetPadding(Box), outSize);
    Vector2 Minimum = UIThemeStyle_GetVector(UIElementLook_GetStyle(&Box->Look, Widget_GetScreen(&Box->Base.Base)), UIElementKind_MenuBar,
        UIMenuBarPart_MinSize);
    outSize->x = Math_MaxFloat(outSize->x, Minimum.x);
    outSize->y = Math_MaxFloat(outSize->y, Minimum.y);
    return Result;
}

static Error MenuBarBox_Render(void* self, UIRenderContext* context)
{
    MenuBarBox* Box = self;
    Rectangle Pixels = UIRenderContext_GetPixelBox(context);
    if (UIElement_IsBoxEmpty(Pixels))
    {
        return Error_CreateSuccess();
    }
    return UIElementLook_DrawBox(&Box->Look, UIElementLook_GetStyle(&Box->Look, Widget_GetScreen(&Box->Base.Base)), UIMenuBarPart_Background,
        context, Pixels, Widget_GetEffectiveScale(&Box->Base.Base));
}

static UIElementLook* MenuBarBox_GetThemeLook(void* self)
{
    return &((MenuBarBox*)self)->Look;
}


// Static functions: the status bar.
static Error StatusBarBox_Construct(void* self, void* args)
{
    StatusBarBox* Box = self;
    const UIStatusBarArgs* Arguments = args;
    Error Result = UIElementLook_Construct(&Box->Look, UIElementKind_StatusBar, Arguments->Options->Element.Variant);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return UIElementText_Construct(&Box->Text, UIKit_GetTextFactory(Arguments->Kit), UIKit_GetAssets(Arguments->Kit),
        UIKit_GetLogger(Arguments->Kit));
}

static Error StatusBarBox_Deconstruct(void* self)
{
    return UIElementText_Deconstruct(&((StatusBarBox*)self)->Text);
}

static Error StatusBarBox_Update(void* self, ProgramTime time)
{
    StatusBarBox* Box = self;
    UIElementLook_Update(&Box->Look, Widget_GetScreen(&Box->Base.Base), UIElementState_Normal, time.PassedTime);
    return UIElementText_Refresh(&Box->Text, &Box->Base.Base, UIStatusBarCapability_GetShownText(&Box->Base.StatusBar));
}

static Error StatusBarBox_Layout(void* self, const UILayoutContext* context)
{
    StatusBarBox* Box = self;
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Box->Look, Widget_GetScreen(&Box->Base.Base));
    return UIStatusBar_ArrangeSections(&Box->Base.Base, context, UIThemeStyle_GetVector(Style, UIElementKind_StatusBar, UIStatusBarPart_Padding),
        UIThemeStyle_GetNumber(Style, UIElementKind_StatusBar, UIStatusBarPart_Spacing));
}

static Error StatusBarBox_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)availableWidth;
    StatusBarBox* Box = self;
    Error Result = UIElementText_Refresh(&Box->Text, &Box->Base.Base, UIStatusBarCapability_GetShownText(&Box->Base.StatusBar));
    const UIScreen* Screen = Widget_GetScreen(&Box->Base.Base);
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Box->Look, Screen);
    Vector2 Padding = UIThemeStyle_GetVector(Style, UIElementKind_StatusBar, UIStatusBarPart_Padding);
    Vector2 Minimum = UIThemeStyle_GetVector(Style, UIElementKind_StatusBar, UIStatusBarPart_MinSize);
    // As tall as a line of the theme's text (a message) and as what its sections hold, whichever is more.
    Vector2 Sections = { .x = 0.0f, .y = 0.0f };
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIStatusBar_MeasureSections(&Box->Base.Base, context, Padding,
            UIThemeStyle_GetNumber(Style, UIElementKind_StatusBar, UIStatusBarPart_Spacing), &Sections);
    }
    float Line = UITheme_GetTextSize(UIScreen_GetTheme(Screen));
    outSize->x = Math_MaxFloat(Minimum.x, Sections.x);
    outSize->y = Math_MaxFloat(Math_MaxFloat(Line + (2.0f * Padding.y), Sections.y), Minimum.y);
    return Result;
}

static Error StatusBarBox_Render(void* self, UIRenderContext* context)
{
    StatusBarBox* Box = self;
    Rectangle Pixels = UIRenderContext_GetPixelBox(context);
    if (UIElement_IsBoxEmpty(Pixels))
    {
        return Error_CreateSuccess();
    }
    const UIScreen* Screen = Widget_GetScreen(&Box->Base.Base);
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Box->Look, Screen);
    Error Result = UIElementLook_DrawBox(&Box->Look, Style, UIStatusBarPart_Background, context, Pixels, Widget_GetEffectiveScale(&Box->Base.Base));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    float Start = 0.0f;
    float End = 1.0f;
    UIStatusBar_GetTextSpan(&Box->Base.Base, &Start, &End);
    Rectangle Span = { .x = Pixels.x + RoundToPixel(Start * Pixels.width), .y = Pixels.y, .width = Math_MaxFloat(0.0f, (End - Start) * Pixels.width),
        .height = Pixels.height };
    Vector2 Text = UIElementText_GetPixelSize(&Box->Text, Screen);
    Color TextColor = UIElementLook_BlendColors(&Box->Look, UIThemeStyle_GetColors(Style, UIStatusBarPart_Text), WHITE);
    RenderContext_PushClip(UIRenderContext_GetRenderContext(context), Span);
    Result = UIElementText_Draw(&Box->Text, context, Span.x, Pixels.y + RoundToPixel((Pixels.height - Text.y) * 0.5f), TextColor);
    RenderContext_PopClip(UIRenderContext_GetRenderContext(context));
    return Result;
}

static UIElementLook* StatusBarBox_GetThemeLook(void* self)
{
    return &((StatusBarBox*)self)->Look;
}


// Static functions: the text box.
static const UIThemeStyle* TextBoxBox_GetStyle(const TextBoxBox* box)
{
    return UIElementLook_GetStyle(&box->Look, Widget_GetScreen(&box->Base.Base));
}

static UIInsets TextBoxBox_GetTextInsets(const void* widget)
{
    const TextBoxBox* Box = widget;
    Vector2 Padding = UIThemeStyle_GetVector(TextBoxBox_GetStyle(Box), UIElementKind_TextBox, UITextBoxPart_Padding);
    return (UIInsets){ .Left = Padding.x, .Top = Padding.y, .Right = Padding.x, .Bottom = Padding.y };
}

static float TextBoxBox_GetLineHeight(const void* widget)
{
    const TextBoxBox* Box = widget;
    return UITheme_GetTextSize(UIScreen_GetTheme(Widget_GetScreen(&Box->Base.Base)));
}

/* The box's border, which its bars stand inside: a flat box's outline, a nine-slice's margins at their size on the screen; none else. */
static UIInsets TextBoxBox_GetBarInsets(const void* widget)
{
    const TextBoxBox* Box = widget;
    int32_t Part = UITextBoxCapability_IsReadOnly(&Box->Base.TextBox) ? UITextBoxPart_ReadOnlyBox : UITextBoxPart_Box;
    return UIStandardLooks_GetBorderInsets(UIThemeStyle_GetBox(TextBoxBox_GetStyle(Box), Part));
}

/* The plain text's colour for the state the box is going to: its document holds it, so it changes only with the state. */
static Color TextBoxBox_GetTextColor(const TextBoxBox* box)
{
    UIElementState State = UITextBoxCapability_GetState(&box->Base.TextBox, &box->Base.Base);
    const UIStateColors* Colors = UIThemeStyle_GetColors(TextBoxBox_GetStyle(box), UITextBoxPart_Text);
    return (Colors != NULL) ? UIStateColors_Resolve(Colors, State, WHITE) : WHITE;
}

static Error TextBoxBox_LayOut(void* widget, bool isWrapped, float wrapWidth, const TextLayout** outLayout, float* outUIPixelsPerUnit)
{
    TextBoxBox* Box = widget;
    *outLayout = NULL;
    *outUIPixelsPerUnit = 0.0f;
    uint32_t Mask = UIThemeStyle_GetCharacter(TextBoxBox_GetStyle(Box), UIElementKind_TextBox, UITextBoxPart_Mask);
    Error Result = UITypingView_RefreshDocument(&Box->Typing, &Box->Base.Base, &Box->Base.TextBox, Mask, TextBoxBox_GetTextColor(Box));
    return (Result.Code == ErrorCode_Success) ? UITypingView_LayOut(&Box->Typing, &Box->Base.Base, isWrapped, wrapWidth, outLayout, outUIPixelsPerUnit)
        : Result;
}

static const UITextBoxGeometry TextBoxBoxGeometry =
{
    .GetTextInsets = TextBoxBox_GetTextInsets,
    .LayOut = TextBoxBox_LayOut,
    .GetLineHeight = TextBoxBox_GetLineHeight,
    .GetBarInsets = TextBoxBox_GetBarInsets
};

static const UITypingParts TextBoxBoxParts =
{
    .Kind = UIElementKind_TextBox,
    .Selection = UITextBoxPart_Selection,
    .InactiveSelection = UITextBoxPart_InactiveSelection,
    .Caret = UITextBoxPart_Caret,
    .OverwriteCaret = UITextBoxPart_OverwriteCaret,
    .CaretWidth = UITextBoxPart_CaretWidth
};

static Error TextBoxBox_Construct(void* self, void* args)
{
    TextBoxBox* Box = self;
    const UITextBoxArgs* Arguments = args;
    Error Result = UIElementLook_Construct(&Box->Look, UIElementKind_TextBox, Arguments->Options->Element.Variant);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = UITypingView_Construct(&Box->Typing, Arguments->Kit);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = UIElementText_Construct(&Box->Placeholder, UIKit_GetTextFactory(Arguments->Kit), UIKit_GetAssets(Arguments->Kit),
        UIKit_GetLogger(Arguments->Kit));
    if (Result.Code != ErrorCode_Success)
    {
        Error Cleanup = UITypingView_Deconstruct(&Box->Typing);
        Error_Deconstruct(&Cleanup);
        return Result;
    }
    UITextBoxCapability_SetGeometry(&Box->Base.TextBox, &TextBoxBoxGeometry);
    return Error_CreateSuccess();
}

static Error TextBoxBox_Deconstruct(void* self)
{
    TextBoxBox* Box = self;
    UITextBoxCapability_SetGeometry(&Box->Base.TextBox, NULL);
    Error First = UITypingView_Deconstruct(&Box->Typing);
    return KeepFirst(First, UIElementText_Deconstruct(&Box->Placeholder));
}

static Error TextBoxBox_Update(void* self, ProgramTime time)
{
    TextBoxBox* Box = self;
    UITextBoxCapability* TextBox = &Box->Base.TextBox;
    UIElementLook_Update(&Box->Look, Widget_GetScreen(&Box->Base.Base), UITextBoxCapability_GetState(TextBox, &Box->Base.Base), time.PassedTime);
    Error Result = UITypingView_Update(&Box->Typing, TextBox, time);
    return (Result.Code == ErrorCode_Success) ? UIElementText_Refresh(&Box->Placeholder, &Box->Base.Base, UITextBoxCapability_GetPlaceholder(TextBox))
        : Result;
}

static Error TextBoxBox_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)availableWidth;
    TextBoxBox* Box = self;
    Vector2 Minimum = UIThemeStyle_GetVector(TextBoxBox_GetStyle(Box), UIElementKind_TextBox, UITextBoxPart_MinSize);
    return UITextBox_MeasurePreferred(&Box->Base.TextBox, &Box->Base.Base, context, Minimum, outSize);
}

static Error TextBoxBox_Render(void* self, UIRenderContext* context)
{
    TextBoxBox* Box = self;
    Rectangle Pixels = UIRenderContext_GetPixelBox(context);
    if (UIElement_IsBoxEmpty(Pixels))
    {
        return Error_CreateSuccess();
    }
    const UITextBoxCapability* TextBox = &Box->Base.TextBox;
    const UIThemeStyle* Style = TextBoxBox_GetStyle(Box);
    float Scale = Widget_GetEffectiveScale(&Box->Base.Base);
    int32_t BoxPart = UITextBoxCapability_IsReadOnly(TextBox) ? UITextBoxPart_ReadOnlyBox : UITextBoxPart_Box;
    Error Result = UIElementLook_DrawBox(&Box->Look, Style, BoxPart, context, Pixels, Scale);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Color Placeholder = UIElementLook_BlendColors(&Box->Look, UIThemeStyle_GetColors(Style, UITextBoxPart_Placeholder), GRAY);
    return UITypingView_Render(&Box->Typing, &Box->Look, &Box->Base.Base, TextBox, context, Style, &TextBoxBoxParts,
        UITypingView_GetViewPixels(&Box->Base.Base, TextBox, Pixels), &Box->Placeholder, Placeholder);
}

static UIElementLook* TextBoxBox_GetThemeLook(void* self)
{
    return &((TextBoxBox*)self)->Look;
}


// Static functions: the backdrop.
static Error BackdropBox_Construct(void* widget, void* args)
{
    (void)args;
    return UIElementLook_Construct(&((BackdropBox*)widget)->Look, UIElementKind_Backdrop, NULL);
}

static Error BackdropBox_Update(void* self, ProgramTime time)
{
    BackdropBox* Box = self;
    UIElementLook_Update(&Box->Look, Widget_GetScreen(&Box->Base), UIElementState_Normal, time.PassedTime);
    return Error_CreateSuccess();
}

static Error BackdropBox_Render(void* self, UIRenderContext* context)
{
    BackdropBox* Box = self;
    Rectangle Pixels = UIRenderContext_GetPixelBox(context);
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Box->Look, Widget_GetScreen(&Box->Base));
    if (UIElement_IsBoxEmpty(Pixels) || (UIThemeStyle_GetBox(Style, UIBackdropPart_Background) == NULL))
    {
        return Error_CreateSuccess(); // A theme that gives none dims nothing: the backdrop still takes the presses.
    }
    return UIElementLook_DrawBox(&Box->Look, Style, UIBackdropPart_Background, context, Pixels, Widget_GetEffectiveScale(&Box->Base));
}

static void* BackdropBox_ResolveLook(void* widget)
{
    return &((BackdropBox*)widget)->Look;
}


// Static functions: the hover text's box.
static Error TooltipBox_Construct(void* widget, void* args)
{
    (void)args;
    TooltipBox* Box = widget;
    UIKit* Kit = UIKit_FromScreen(Widget_GetScreen(&Box->Base));
    if (Kit == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UI hover text: the screen's factory has no kit.");
    }
    Error Result = UIElementLook_Construct(&Box->Look, UIElementKind_Tooltip, NULL);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UITextContent_Construct(&Box->Content, NULL);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = UIElementText_Construct(&Box->Text, UIKit_GetTextFactory(Kit), UIKit_GetAssets(Kit), UIKit_GetLogger(Kit));
    if (Result.Code != ErrorCode_Success)
    {
        UITextContent_Deconstruct(&Box->Content);
        return Result;
    }
    Box->IsShowing = false;
    return Widget_SetIsInputEnabled(&Box->Base, false); // Only the flag: the widget is not initialized yet.
}

static Error TooltipBox_OnDeconstruct(void* self)
{
    TooltipBox* Box = self;
    UITextContent_Deconstruct(&Box->Content);
    return UIElementText_Deconstruct(&Box->Text);
}

/* Lays the text out at the theme's wrap width. */
static Error TooltipBox_RefreshText(TooltipBox* box)
{
    const UIThemeStyle* Style = UIElementLook_GetStyle(&box->Look, Widget_GetScreen(&box->Base));
    float MaxWidth = UIThemeStyle_GetNumber(Style, UIElementKind_Tooltip, UITooltipPart_MaxWidth);
    UIElementText_SetWrapWidth(&box->Text, MaxWidth > 0.0f, MaxWidth);
    return UIElementText_Refresh(&box->Text, &box->Base, &box->Content);
}

static Error TooltipBox_Update(void* self, ProgramTime time)
{
    TooltipBox* Box = self;
    UIScreen* Screen = Widget_GetScreen(&Box->Base);
    UIElementLook_Update(&Box->Look, Screen, UIElementState_Normal, time.PassedTime);
    UITooltipState State;
    Box->IsShowing = UITooltip_GetCurrent(Screen, &State) && State.IsShown;
    if (Box->IsShowing)
    {
        bool HasChanged = false;
        Error Result = UITextContent_Set(&Box->Content, State.Text, &HasChanged);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        if (HasChanged)
        {
            Widget_InvalidateMeasure(&Box->Base);
        }
    }
    return TooltipBox_RefreshText(Box);
}

static Error TooltipBox_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)context;
    (void)availableWidth;
    TooltipBox* Box = self;
    Error Result = TooltipBox_RefreshText(Box);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    const UIScreen* Screen = Widget_GetScreen(&Box->Base);
    Vector2 Text = UIElementText_GetPixelSize(&Box->Text, Screen);
    Vector2 Padding = UIThemeStyle_GetVector(UIElementLook_GetStyle(&Box->Look, Screen), UIElementKind_Tooltip, UITooltipPart_Padding);
    float Scale = Widget_GetEffectiveScale(&Box->Base);
    outSize->x = (Text.x / Scale) + (2.0f * Padding.x);
    outSize->y = (Text.y / Scale) + (2.0f * Padding.y);
    return Error_CreateSuccess();
}

/* Writes the box's position and size, each only when it changes: every write makes the screen hit-test again, and an idle screen
 * must not. */
static Error TooltipBox_SetBox(TooltipBox* box, Vector2 position, Vector2 size)
{
    Vector2 OldPosition = Widget_GetPosition(&box->Base);
    if ((OldPosition.x != position.x) || (OldPosition.y != position.y))
    {
        Error Result = Widget_SetPosition(&box->Base, position);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    Vector2 OldSize = Widget_GetSize(&box->Base);
    return ((OldSize.x != size.x) || (OldSize.y != size.y)) ? Widget_SetSize(&box->Base, size) : Error_CreateSuccess();
}

/* Beside the pointer, by the theme's offset (negative: left of or above it), and kept on the surface: past its left or right edge it
 * slides in; past its top or bottom it goes to the other side of the pointer (the offset mirrored), and is kept on the surface
 * even then. */
static Error TooltipBox_Layout(void* self, const UILayoutContext* context)
{
    TooltipBox* Box = self;
    UITooltipState State;
    if (!Box->IsShowing || !UITooltip_GetCurrent(Widget_GetScreen(&Box->Base), &State))
    {
        return TooltipBox_SetBox(Box, Widget_GetPosition(&Box->Base), (Vector2){ .x = 0.0f, .y = 0.0f });
    }
    Vector2 Size = { .x = 0.0f, .y = 0.0f };
    Error Result = Widget_Measure(&Box->Base, context->Sizing, UI_MEASURE_UNBOUNDED_WIDTH, &Size);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Vector2 Surface = context->SurfaceSizePixels;
    float Scale = UIScreen_GetScale(Widget_GetScreen(&Box->Base)); // A root's offsets and size count in the screen's scale.
    Vector2 Offset = UIThemeStyle_GetVector(UIElementLook_GetStyle(&Box->Look, Widget_GetScreen(&Box->Base)), UIElementKind_Tooltip,
        UITooltipPart_Offset);
    float Width = RoundToPixel(Size.x * Scale);
    float Height = RoundToPixel(Size.y * Scale);
    float PointerX = State.PointerPosition.x * Surface.x;
    float PointerY = State.PointerPosition.y * Surface.y;
    float X = RoundToPixel(PointerX + (Offset.x * Scale));
    float Y = RoundToPixel(PointerY + (Offset.y * Scale));
    if ((X + Width) > Surface.x)
    {
        X = Surface.x - Width;
    }
    if (((Y + Height) > Surface.y) || (Y < 0.0f))
    {
        Y = RoundToPixel(PointerY - (Offset.y * Scale)) - Height;
    }
    X = Math_MaxFloat(0.0f, X);
    Y = Math_MaxFloat(0.0f, Math_MinFloat(Y, Surface.y - Height));
    return TooltipBox_SetBox(Box, (Vector2){ .x = X / Surface.x, .y = Y / Surface.y },
        (Vector2){ .x = Width / Surface.x, .y = Height / Surface.y });
}

static Error TooltipBox_Render(void* self, UIRenderContext* context)
{
    TooltipBox* Box = self;
    Rectangle Pixels = UIRenderContext_GetPixelBox(context);
    if (!Box->IsShowing || UIElement_IsBoxEmpty(Pixels))
    {
        return Error_CreateSuccess();
    }
    const UIScreen* Screen = Widget_GetScreen(&Box->Base);
    float Scale = Widget_GetEffectiveScale(&Box->Base);
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Box->Look, Screen);
    Error Result = UIElementLook_DrawBox(&Box->Look, Style, UITooltipPart_Background, context, Pixels, Scale);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Vector2 Padding = UIThemeStyle_GetVector(Style, UIElementKind_Tooltip, UITooltipPart_Padding);
    Color TextColor = UIElementLook_BlendColors(&Box->Look, UIThemeStyle_GetColors(Style, UITooltipPart_Text), WHITE);
    return UIElementText_Draw(&Box->Text, context, Pixels.x + RoundToPixel(Padding.x * Scale), Pixels.y + RoundToPixel(Padding.y * Scale),
        TextColor);
}

static void* TooltipBox_ResolveLook(void* widget)
{
    return &((TooltipBox*)widget)->Look;
}


// Fields.
static const WidgetVTable TooltipBoxVTable =
{
    .OnDeconstruct = TooltipBox_OnDeconstruct,
    .Update = TooltipBox_Update,
    .Layout = TooltipBox_Layout,
    .Render = TooltipBox_Render,
    .Measure = TooltipBox_Measure
};

static const WidgetVTable BackdropBoxVTable =
{
    .Update = BackdropBox_Update,
    .Render = BackdropBox_Render
};

static const UILookVTable PanelBoxLook =
{
    .Construct = PanelBox_Construct,
    .Update = PanelBox_Update,
    .Layout = PanelBox_Layout,
    .Render = PanelBox_Render,
    .Measure = PanelBox_Measure,
    .GetThemeLook = PanelBox_GetThemeLook
};

static const UILookVTable ButtonBoxLook =
{
    .Construct = ButtonBox_Construct,
    .Deconstruct = ButtonBox_Deconstruct,
    .Update = ButtonBox_Update,
    .Render = ButtonBox_Render,
    .Measure = ButtonBox_Measure,
    .GetThemeLook = ButtonBox_GetThemeLook
};

static const UILookVTable ToggleBoxLook =
{
    .Construct = ToggleBox_Construct,
    .Deconstruct = ToggleBox_Deconstruct,
    .Update = ToggleBox_Update,
    .Render = ToggleBox_Render,
    .Measure = ToggleBox_Measure,
    .GetThemeLook = ToggleBox_GetThemeLook
};

static const UILookVTable SliderBoxLook =
{
    .Construct = SliderBox_Construct,
    .Update = SliderBox_Update,
    .Render = SliderBox_Render,
    .Measure = SliderBox_Measure,
    .GetThemeLook = SliderBox_GetThemeLook
};

static const UILookVTable ScrollbarBoxLook =
{
    .Construct = ScrollbarBox_Construct,
    .Update = ScrollbarBox_Update,
    .Render = ScrollbarBox_Render,
    .Measure = ScrollbarBox_Measure,
    .GetThemeLook = ScrollbarBox_GetThemeLook
};

static const UILookVTable DropdownBoxLook =
{
    .Construct = DropdownBox_Construct,
    .Deconstruct = DropdownBox_Deconstruct,
    .Update = DropdownBox_Update,
    .Render = DropdownBox_Render,
    .Measure = DropdownBox_Measure,
    .GetThemeLook = DropdownBox_GetThemeLook
};

static const UILookVTable MenuBarBoxLook =
{
    .Construct = MenuBarBox_Construct,
    .Update = MenuBarBox_Update,
    .Layout = MenuBarBox_Layout,
    .Render = MenuBarBox_Render,
    .Measure = MenuBarBox_Measure,
    .GetThemeLook = MenuBarBox_GetThemeLook
};

static const UILookVTable StatusBarBoxLook =
{
    .Construct = StatusBarBox_Construct,
    .Deconstruct = StatusBarBox_Deconstruct,
    .Update = StatusBarBox_Update,
    .Layout = StatusBarBox_Layout,
    .Render = StatusBarBox_Render,
    .Measure = StatusBarBox_Measure,
    .GetThemeLook = StatusBarBox_GetThemeLook
};

static const UILookVTable TextBoxBoxLook =
{
    .Construct = TextBoxBox_Construct,
    .Deconstruct = TextBoxBox_Deconstruct,
    .Update = TextBoxBox_Update,
    .Render = TextBoxBox_Render,
    .Measure = TextBoxBox_Measure,
    .GetThemeLook = TextBoxBox_GetThemeLook
};

static const UILookVTable ScrollAreaBoxLook =
{
    .Construct = ScrollAreaBox_Construct,
    .Update = ScrollAreaBox_Update,
    .Render = ScrollAreaBox_Render,
    .Measure = ScrollAreaBox_Measure,
    .GetThemeLook = ScrollAreaBox_GetThemeLook
};


// Public functions.
Error UIStandardLooks_RegisterBackdrop(UIKit* kit, UIWidgetFactory* factory, uint64_t* outTypeId)
{
    WidgetCapabilityEntry Capabilities[] =
    {
        { .CapabilityId = UIKit_GetCapabilityId(kit, UIElementCapability_Look), .Resolver = BackdropBox_ResolveLook }
    };
    WidgetTypeDescriptor Descriptor =
    {
        .Name = u8"Backdrop",
        .StructSize = sizeof(BackdropBox),
        .VTable = &BackdropBoxVTable,
        .Constructor = BackdropBox_Construct,
        .Capabilities = Capabilities,
        .CapabilityCount = sizeof(Capabilities) / sizeof(Capabilities[0]),
        .Properties = NULL,
        .PropertyCount = 0U,
        .IsFocusable = false
    };
    return UIWidgetFactory_RegisterType(factory, &Descriptor, outTypeId);
}

Error UIStandardLooks_RegisterTooltip(UIKit* kit, UIWidgetFactory* factory, uint64_t* outTypeId)
{
    WidgetCapabilityEntry Capabilities[] =
    {
        { .CapabilityId = UIKit_GetCapabilityId(kit, UIElementCapability_Look), .Resolver = TooltipBox_ResolveLook }
    };
    WidgetTypeDescriptor Descriptor =
    {
        .Name = u8"Tooltip",
        .StructSize = sizeof(TooltipBox),
        .VTable = &TooltipBoxVTable,
        .Constructor = TooltipBox_Construct,
        .Capabilities = Capabilities,
        .CapabilityCount = sizeof(Capabilities) / sizeof(Capabilities[0]),
        .Properties = NULL,
        .PropertyCount = 0U,
        .IsFocusable = false
    };
    return UIWidgetFactory_RegisterType(factory, &Descriptor, outTypeId);
}

Error UIStandardLooks_Register(UIKit* kit)
{
    Error Result = UIKit_RegisterLook(kit, UIElementKind_Panel, UI_KIT_STANDARD_LOOK, sizeof(PanelBox), &PanelBoxLook, NULL);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIKit_RegisterLook(kit, UIElementKind_Button, UI_KIT_STANDARD_LOOK, sizeof(ButtonBox), &ButtonBoxLook, NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIKit_RegisterLook(kit, UIElementKind_Toggle, UI_KIT_STANDARD_LOOK, sizeof(ToggleBox), &ToggleBoxLook, NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIKit_RegisterLook(kit, UIElementKind_Slider, UI_KIT_STANDARD_LOOK, sizeof(SliderBox), &SliderBoxLook, NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIKit_RegisterLook(kit, UIElementKind_Scrollbar, UI_KIT_STANDARD_LOOK, sizeof(ScrollbarBox), &ScrollbarBoxLook, NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIKit_RegisterLook(kit, UIElementKind_ScrollArea, UI_KIT_STANDARD_LOOK, sizeof(ScrollAreaBox), &ScrollAreaBoxLook, NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIKit_RegisterLook(kit, UIElementKind_Dropdown, UI_KIT_STANDARD_LOOK, sizeof(DropdownBox), &DropdownBoxLook, NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIKit_RegisterLook(kit, UIElementKind_MenuBar, UI_KIT_STANDARD_LOOK, sizeof(MenuBarBox), &MenuBarBoxLook, NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIKit_RegisterLook(kit, UIElementKind_StatusBar, UI_KIT_STANDARD_LOOK, sizeof(StatusBarBox), &StatusBarBoxLook, NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIKit_RegisterLook(kit, UIElementKind_TextBox, UI_KIT_STANDARD_LOOK, sizeof(TextBoxBox), &TextBoxBoxLook, NULL);
    }
    return (Result.Code == ErrorCode_Success) ? UIStandardValueLooks_Register(kit) : Result;
}
