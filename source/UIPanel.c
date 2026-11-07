#include "UIPanel.h"
#include "UIKitInternal.h"
#include "UIScreen.h"
#include "UIWidgetFactory.h"


// Static functions: the kind's behaviour, for the kit's composed widget types.
static UIPanelCapability* GetCapability(void* widget)
{
    return &((UIPanelLookBase*)widget)->Panel;
}

static Error Behaviour_Construct(void* widget, void* args)
{
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIPanel: the options are required.");
    }
    return UIPanelCapability_Construct(GetCapability(widget), widget, args);
}

/* A look without a frame of its own: the options' padding and spacing, or none. */
static Error Behaviour_Layout(void* widget, const UILayoutContext* context)
{
    UIStackLayout Layout = UIPanel_GetLayout(GetCapability(widget), UIInsets_All(0.0f), 0.0f);
    return UIStackLayout_Arrange(&Layout, widget, context);
}


// Fields.
static const UIKindBehaviour PanelBehaviour =
{
    .Kind = UIElementKind_Panel,
    .HasCapability = false,
    .Capability = UIElementCapability_Look,
    .ResolveCapability = NULL,
    .BaseSize = sizeof(UIPanelLookBase),
    .IsFocusable = false,
    .NeedsEntering = false,
    .EntersItself = false,
    .Construct = Behaviour_Construct,
    .Layout = Behaviour_Layout
};


// Internal functions (declared in UIKitInternal.h).
const UIKindBehaviour* UIPanel_GetKindBehaviour(void)
{
    return &PanelBehaviour;
}


// Public functions.
Error UIPanelCapability_Construct(UIPanelCapability* self, Widget* widget, const UIPanelOptions* options)
{
    UIStackLayout Check = { .Direction = options->Direction, .Padding = options->Padding, .Spacing = options->Spacing };
    if (!UIStackLayout_IsValid(&Check))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "UIPanel: a known direction, and padding and spacing within [0, UI_LAYOUT_MAX_LENGTH], are required.");
    }
    self->_direction = options->Direction;
    self->_hasPadding = options->HasPadding;
    self->_padding = options->Padding;
    self->_hasSpacing = options->HasSpacing;
    self->_spacing = options->Spacing;
    return UIElement_ApplyOptions(widget, &options->Element);
}

UIStackLayout UIPanel_GetLayout(const UIPanelCapability* self, UIInsets lookPadding, float lookSpacing)
{
    return (UIStackLayout)
    {
        .Direction = self->_direction,
        .Padding = self->_hasPadding ? self->_padding : lookPadding,
        .Spacing = self->_hasSpacing ? self->_spacing : lookSpacing
    };
}
