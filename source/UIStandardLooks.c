#include "UIStandardLooks.h"
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

static Error SliderBox_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)context;
    (void)availableWidth;
    SliderBox* Box = self;
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Box->Look, Widget_GetScreen(&Box->Base.Base));
    Vector2 Minimum = UIThemeStyle_GetVector(Style, UIElementKind_Slider, UISliderPart_MinSize);
    Vector2 Thumb = UIThemeStyle_GetVector(Style, UIElementKind_Slider, UISliderPart_ThumbSize);
    outSize->x = Math_MaxFloat(Minimum.x, Thumb.x);
    outSize->y = Math_MaxFloat(Minimum.y, Thumb.y);
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

    float Thickness = Math_MaxFloat(1.0f, RoundToPixel(UIThemeStyle_GetNumber(Style, UIElementKind_Slider, UISliderPart_TrackThickness) * Scale));
    Vector2 ThumbSize = UIThemeStyle_GetVector(Style, UIElementKind_Slider, UISliderPart_ThumbSize);
    float ThumbWidth = RoundToPixel(ThumbSize.x * Scale);
    float ThumbHeight = RoundToPixel(ThumbSize.y * Scale);
    float Fraction = (float)UISlider_GetFraction(Slider->_value, Slider->_minimum, Slider->_maximum);
    float ThumbX = Pixels.x + RoundToPixel(Fraction * Math_MaxFloat(0.0f, Pixels.width - ThumbWidth));

    Rectangle Track = { .x = Pixels.x, .y = Pixels.y + RoundToPixel((Pixels.height - Thickness) * 0.5f), .width = Pixels.width,
        .height = Thickness };
    Rectangle Fill = Track;
    Fill.width = (ThumbX + (ThumbWidth * 0.5f)) - Pixels.x;
    Rectangle Thumb = { .x = ThumbX, .y = Pixels.y + RoundToPixel((Pixels.height - ThumbHeight) * 0.5f), .width = ThumbWidth,
        .height = ThumbHeight };
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


// Public functions.
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
    return Result;
}
