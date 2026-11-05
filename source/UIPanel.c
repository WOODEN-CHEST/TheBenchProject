#include "UIPanel.h"
#include "UIScreen.h"
#include "UIWidgetFactory.h"
#include "wr/WRCompile.h"
#include "wr/WRMath.h"


// Types.
/* The standard panel widget. */
typedef struct UIPanelWidgetStruct
{
    Widget Base;
    UIPanelCapability Panel;
} UIPanelWidget;


// Static functions.
/* How the panel lays its children out, with the theme's padding and spacing where its options give none. */
static UIStackLayout GetLayout(const UIPanelCapability* self, const UIScreen* screen)
{
    const UIThemeStyle* Style = UIElementLook_GetStyle(&self->_look, screen);
    Vector2 ThemePadding = UIThemeStyle_GetVector(Style, UIElementKind_Panel, UIPanelPart_Padding);
    return (UIStackLayout)
    {
        .Direction = self->_direction,
        .Padding = self->_hasPadding ? self->_padding
            : (UIInsets){ .Left = ThemePadding.x, .Top = ThemePadding.y, .Right = ThemePadding.x, .Bottom = ThemePadding.y },
        .Spacing = self->_hasSpacing ? self->_spacing : UIThemeStyle_GetNumber(Style, UIElementKind_Panel, UIPanelPart_Spacing)
    };
}


// Static functions: the standard widget's hooks.
static Error UIPanelWidget_ConstructVT(void* widget, void* args)
{
    UIPanelWidget* Panel = widget;
    const UIPanelOptions* Options = args;
    if (Options == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIPanel: the options are required.");
    }
    return UIPanelCapability_Construct(&Panel->Panel, &Panel->Base, Options);
}

static Error UIPanelWidget_UpdateVT(void* self, ProgramTime time)
{
    UIPanelWidget* Panel = self;
    UIPanelCapability_Update(&Panel->Panel, &Panel->Base, time);
    return Error_CreateSuccess();
}

static Error UIPanelWidget_LayoutVT(void* self, const UILayoutContext* context)
{
    UIPanelWidget* Panel = self;
    return UIPanelCapability_Layout(&Panel->Panel, &Panel->Base, context);
}

static Error UIPanelWidget_MeasureVT(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    UIPanelWidget* Panel = self;
    return UIPanelCapability_Measure(&Panel->Panel, &Panel->Base, context, availableWidth, outSize);
}

static Error UIPanelWidget_RenderVT(void* self, UIRenderContext* context)
{
    UIPanelWidget* Panel = self;
    UIScreen* Screen = Widget_GetScreen(&Panel->Base);
    Rectangle Box = UIRenderContext_GetPixelBox(context);
    if (UIElement_IsBoxEmpty(Box))
    {
        return Error_CreateSuccess();
    }
    return UIElementLook_DrawBox(&Panel->Panel._look, UIElementLook_GetStyle(&Panel->Panel._look, Screen), UIPanelPart_Background,
        context, Box, UIScreen_GetScale(Screen));
}

static void* ResolveLook(void* widget)
{
    return &((UIPanelWidget*)widget)->Panel._look;
}


// Fields.
static const WidgetVTable UIPanelWidgetVTable =
{
    .Update = UIPanelWidget_UpdateVT,
    .Layout = UIPanelWidget_LayoutVT,
    .Render = UIPanelWidget_RenderVT,
    .Measure = UIPanelWidget_MeasureVT
};


// Public functions.
Error UIPanelCapability_Construct(UIPanelCapability* self, Widget* widget, const UIPanelOptions* options)
{
    UIStackLayout Check = { .Direction = options->Direction, .Padding = options->Padding, .Spacing = options->Spacing };
    if (!UIStackLayout_IsValid(&Check))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "UIPanel: a known direction, and padding and spacing within [0, UI_LAYOUT_MAX_LENGTH], are required.");
    }
    Error Result = UIElementLook_Construct(&self->_look, UIElementKind_Panel, options->Element.Variant);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    self->_direction = options->Direction;
    self->_hasPadding = options->HasPadding;
    self->_padding = options->Padding;
    self->_hasSpacing = options->HasSpacing;
    self->_spacing = options->Spacing;
    return UIElement_ApplyOptions(widget, &options->Element);
}

void UIPanelCapability_Update(UIPanelCapability* self, Widget* widget, ProgramTime time)
{
    UIElementLook_Update(&self->_look, Widget_GetScreen(widget), UIElementState_Normal, time.PassedTime);
}

Error UIPanelCapability_Layout(UIPanelCapability* self, Widget* widget, const UILayoutContext* context)
{
    UIStackLayout Layout = GetLayout(self, Widget_GetScreen(widget));
    return UIStackLayout_Arrange(&Layout, widget, context);
}

Error UIPanelCapability_Measure(UIPanelCapability* self, Widget* widget, const UIMeasureContext* context, float availableWidth,
    Vector2* outSize)
{
    const UIScreen* Screen = Widget_GetScreen(widget);
    UIStackLayout Layout = GetLayout(self, Screen);
    Error Result = UIStackLayout_Measure(&Layout, widget, context, availableWidth, outSize);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Vector2 Minimum = UIThemeStyle_GetVector(UIElementLook_GetStyle(&self->_look, Screen), UIElementKind_Panel, UIPanelPart_MinSize);
    outSize->x = Math_MaxFloat(outSize->x, Minimum.x);
    outSize->y = Math_MaxFloat(outSize->y, Minimum.y);
    return Error_CreateSuccess();
}

Error UIPanel_RegisterType(UIWidgetFactory* factory, uint64_t lookCapabilityId, uint64_t* outTypeId)
{
    WidgetCapabilityEntry Capabilities[] = { { .CapabilityId = lookCapabilityId, .Resolver = ResolveLook } };
    WidgetTypeDescriptor Descriptor =
    {
        .Name = u8"Panel",
        .StructSize = sizeof(UIPanelWidget),
        .VTable = &UIPanelWidgetVTable,
        .Constructor = UIPanelWidget_ConstructVT,
        .Capabilities = Capabilities,
        .CapabilityCount = sizeof(Capabilities) / sizeof(Capabilities[0]),
        .Properties = NULL,
        .PropertyCount = 0U,
        .IsFocusable = false
    };
    return UIWidgetFactory_RegisterType(factory, &Descriptor, outTypeId);
}
