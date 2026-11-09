#include "UILabelled.h"
#include "UIKit.h"
#include "UIKitInternal.h"
#include "UILabel.h"
#include "UIScreen.h"
#include "wr/WRMath.h"


// Static functions: small helpers.
static float ToLength(float value)
{
    return ((value > 0.0f) && !Math_IsInfinityFloat(value)) ? value : 0.0f;
}

static bool IsLayoutValid(UILabelledLayout layout)
{
    return (layout == UILabelledLayout_Across) || (layout == UILabelledLayout_Stacked);
}

static UILabelledCapability* GetLabelled(void* widget)
{
    return &((UILabelledLookBase*)widget)->Labelled;
}

static UIFormCapability* GetForm(void* widget)
{
    return &((UIFormLookBase*)widget)->Form;
}

static Error ResolveLabelled(UIScreen* screen, uint64_t widgetId, Widget** outWidget, UILabelledCapability** outLabelled)
{
    UIKit* Kit = (screen != NULL) ? UIKit_FromScreen(screen) : NULL;
    Widget* Target = NULL;
    if ((Kit == NULL) || !UIScreen_TryGetWidgetById(screen, widgetId, &Target))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UILabelled: the widget is gone, or its screen has no kit.");
    }
    UILabelledCapability* Labelled = Widget_GetCapability(Target, UIKit_GetCapabilityId(Kit, UIElementCapability_Labelled));
    if (Labelled == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UILabelled: the widget is not a labelled element.");
    }
    *outWidget = Target;
    *outLabelled = Labelled;
    return Error_CreateSuccess();
}

/* The labelled element's capability of a widget, when it is one; NULL else. */
static UILabelledCapability* FindLabelled(const Widget* widget)
{
    UIKit* Kit = UIKit_FromScreen(Widget_GetScreen(widget));
    return (Kit != NULL) ? Widget_GetCapability((Widget*)widget, UIKit_GetCapabilityId(Kit, UIElementCapability_Labelled)) : NULL;
}

/* The label and the element it labels (its first child other than the label that shows), each NULL when there is none. */
static void FindParts(Widget* widget, const UILabelledCapability* self, Widget** outLabel, Widget** outElement)
{
    UIScreen* Screen = Widget_GetScreen(widget);
    *outLabel = NULL;
    *outElement = NULL;
    Widget* Label = NULL;
    if (UIScreen_TryGetWidgetById(Screen, self->_labelId, &Label) && (Widget_GetParent(Label) == widget) && Widget_IsRendered(Label))
    {
        *outLabel = Label;
    }
    size_t Count = Widget_GetSubWidgetCount(widget);
    for (size_t Index = 0U; Index < Count; Index++)
    {
        Widget* Child = Widget_GetSubWidgetAt(widget, Index);
        if ((Child != NULL) && (Widget_GetId(Child) != self->_labelId) && Widget_IsRendered(Child))
        {
            *outElement = Child;
            return;
        }
    }
}

/* The label's own size, in the labelled element's units; zero with no label or an empty one. */
static Error MeasureLabel(Widget* label, const UIMeasureContext* context, Vector2* outSize)
{
    *outSize = (Vector2){ .x = 0.0f, .y = 0.0f };
    return (label != NULL) ? Widget_Measure(label, context, UI_MEASURE_UNBOUNDED_WIDTH, outSize) : Error_CreateSuccess();
}

/* Whether a widget is a row of a form (its parent is one). */
static bool IsInForm(const Widget* widget)
{
    const Widget* Parent = Widget_GetParent(widget);
    UIKit* Kit = UIKit_FromScreen(Widget_GetScreen(widget));
    return (Parent != NULL) && (Kit != NULL) && (Widget_GetCapability((Widget*)Parent, UIKit_GetCapabilityId(Kit, UIElementCapability_Form)) != NULL);
}

/* The label column's width: its own, a form's shared one (across, while it is the form's row), or the label's. */
static float ChooseLabelWidth(const Widget* widget, const UILabelledCapability* self, float labelOwnWidth)
{
    if (self->_hasLabelWidth)
    {
        return self->_labelWidth;
    }
    if (self->_hasSharedLabelWidth && IsInForm(widget))
    {
        return self->_sharedLabelWidth;
    }
    return labelOwnWidth;
}

/* A labelled element's size with its label column @p labelWidth wide: what its Measure answers with its own choice, and a form with the
 * shared width. Reads only. */
static Error MeasureWithLabelWidth(Widget* widget, const UIMeasureContext* context, float availableWidth, float labelWidth, Vector2* outSize)
{
    const UILabelledCapability* Self = GetLabelled(widget);
    Widget* Label = NULL;
    Widget* Element = NULL;
    FindParts(widget, Self, &Label, &Element);
    Vector2 LabelSize = { .x = 0.0f, .y = 0.0f };
    Error Result = MeasureLabel(Label, context, &LabelSize);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    bool IsAcross = Self->_layout == UILabelledLayout_Across;
    float Spacing = ((Label != NULL) && (Element != NULL) && ((IsAcross ? labelWidth : LabelSize.y) > 0.0f)) ? Self->_spacing : 0.0f;
    float ElementWidth = availableWidth;
    if (IsAcross && (availableWidth < UI_MEASURE_UNBOUNDED_WIDTH))
    {
        ElementWidth = Math_MaxFloat(0.0f, availableWidth - labelWidth - Spacing);
    }
    Vector2 ElementSize = { .x = 0.0f, .y = 0.0f };
    if (Element != NULL)
    {
        Result = Widget_Measure(Element, context, ElementWidth, &ElementSize);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    if (IsAcross)
    {
        *outSize = (Vector2){ .x = labelWidth + Spacing + ElementSize.x, .y = Math_MaxFloat(LabelSize.y, ElementSize.y) };
    }
    else
    {
        *outSize = (Vector2){ .x = Math_MaxFloat(LabelSize.x, ElementSize.x), .y = LabelSize.y + Spacing + ElementSize.y };
    }
    return Error_CreateSuccess();
}

/* The form's rows that line up: the labelled ones across that have no label width of their own. */
static bool IsSharingRow(const Widget* child)
{
    const UILabelledCapability* Labelled = FindLabelled(child);
    return (Labelled != NULL) && (Labelled->_layout == UILabelledLayout_Across) && !Labelled->_hasLabelWidth;
}

/* The widest label among a form's rows that line up. */
static Error MeasureSharedLabelWidth(Widget* form, const UIMeasureContext* context, float* outWidth)
{
    *outWidth = 0.0f;
    size_t Count = Widget_GetSubWidgetCount(form);
    for (size_t Index = 0U; Index < Count; Index++)
    {
        Widget* Child = Widget_GetSubWidgetAt(form, Index);
        if ((Child == NULL) || !Widget_IsRendered(Child) || !IsSharingRow(Child))
        {
            continue;
        }
        Widget* Label = NULL;
        Widget* Element = NULL;
        FindParts(Child, FindLabelled(Child), &Label, &Element);
        Vector2 Size = { .x = 0.0f, .y = 0.0f };
        Error Result = MeasureLabel(Label, context, &Size);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        // The label measures in its row's units; the row sits in the form at its own scale.
        float RowScale = Widget_GetScale(Child);
        *outWidth = Math_MaxFloat(*outWidth, Size.x * ((RowScale > 0.0f) ? RowScale : 1.0f));
    }
    return Error_CreateSuccess();
}

/* A form's row's size: a row that lines up at the shared width, any other as it measures. In the form's units. */
static Error MeasureRow(Widget* child, const UIMeasureContext* context, float availableWidth, float sharedWidth, Vector2* outSize)
{
    if (!IsSharingRow(child))
    {
        return Widget_Measure(child, context, availableWidth, outSize);
    }
    // Widget_Measure's conversion by hand: the row measures in its own units, at its scale.
    float RowScale = Widget_GetScale(child);
    float Scale = (RowScale > 0.0f) ? RowScale : 1.0f;
    float Available = (availableWidth < UI_MEASURE_UNBOUNDED_WIDTH) ? (availableWidth / Scale) : availableWidth;
    Vector2 Size = { .x = 0.0f, .y = 0.0f };
    Error Result = MeasureWithLabelWidth(child, context, Available, sharedWidth / Scale, &Size);
    *outSize = (Vector2){ .x = Size.x * Scale, .y = Size.y * Scale };
    return Result;
}


// Static functions: the kinds' behaviours.
static void* ResolveLabelledCapability(void* widget)
{
    return GetLabelled(widget);
}

static void* ResolveFormCapability(void* widget)
{
    return GetForm(widget);
}

static Error Labelled_Construct(void* widget, void* args)
{
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UILabelled: the arguments are required.");
    }
    const UILabelledOptions* Options = ((const UILabelledArgs*)args)->Options;
    if (!IsLayoutValid(Options->Layout) || (Options->HasLabelWidth && (Math_IsNaNFloat(Options->LabelWidth) || (Options->LabelWidth < 0.0f)
        || Math_IsInfinityFloat(Options->LabelWidth))))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UILabelled: the layout or the label width is not valid.");
    }
    Error Result = UIElement_ApplyOptions(widget, &Options->Element);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    UILabelledCapability* Self = GetLabelled(widget);
    Self->_labelId = WIDGET_ID_INVALID;
    Self->_layout = Options->Layout;
    Self->_hasLabelWidth = Options->HasLabelWidth;
    Self->_labelWidth = Options->HasLabelWidth ? Options->LabelWidth : 0.0f;
    Self->_hasSharedLabelWidth = false;
    Self->_sharedLabelWidth = 0.0f;
    Self->_spacing = 0.0f;
    return Error_CreateSuccess();
}

static Error Labelled_Layout(void* widget, const UILayoutContext* context)
{
    return UILabelled_Arrange(widget, context);
}

static Error Form_Construct(void* widget, void* args)
{
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIForm: the arguments are required.");
    }
    const UIFormOptions* Options = ((const UIFormArgs*)args)->Options;
    Error Result = UIElement_ApplyOptions(widget, &Options->Element);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    UIFormCapability* Self = GetForm(widget);
    Self->_padding = (Vector2){ .x = 0.0f, .y = 0.0f };
    Self->_spacing = 0.0f;
    Self->_labelWidth = 0.0f;
    return Error_CreateSuccess();
}

static Error Form_Layout(void* widget, const UILayoutContext* context)
{
    return UIForm_Arrange(widget, context);
}


// Fields.
static const UIKindCapability LabelledCapabilities[] = { { .Capability = UIElementCapability_Labelled, .Resolve = ResolveLabelledCapability } };

static const UIKindBehaviour LabelledBehaviour =
{
    .Kind = UIElementKind_Labelled,
    .Capabilities = LabelledCapabilities,
    .CapabilityCount = sizeof(LabelledCapabilities) / sizeof(LabelledCapabilities[0]),
    .BaseSize = sizeof(UILabelledLookBase),
    .IsFocusable = false,
    .NeedsEntering = false,
    .EntersItself = false,
    .Construct = Labelled_Construct,
    .Layout = Labelled_Layout
};

static const UIKindCapability FormCapabilities[] = { { .Capability = UIElementCapability_Form, .Resolve = ResolveFormCapability } };

static const UIKindBehaviour FormBehaviour =
{
    .Kind = UIElementKind_Form,
    .Capabilities = FormCapabilities,
    .CapabilityCount = sizeof(FormCapabilities) / sizeof(FormCapabilities[0]),
    .BaseSize = sizeof(UIFormLookBase),
    .IsFocusable = false,
    .NeedsEntering = false,
    .EntersItself = false,
    .Construct = Form_Construct,
    .Layout = Form_Layout
};


// Internal functions (declared in UIKitInternal.h).
const UIKindBehaviour* UILabelled_GetKindBehaviour(void)
{
    return &LabelledBehaviour;
}

const UIKindBehaviour* UIForm_GetKindBehaviour(void)
{
    return &FormBehaviour;
}


// Public functions: calling code.
Error UILabelled_SetText(UIScreen* screen, uint64_t widgetId, const unsigned char* text)
{
    Widget* Target = NULL;
    UILabelledCapability* Self = NULL;
    Error Result = ResolveLabelled(screen, widgetId, &Target, &Self);
    return (Result.Code == ErrorCode_Success) ? UILabel_SetText(screen, Self->_labelId, (text != NULL) ? text : (const unsigned char*)u8"")
        : Result;
}

Error UILabelled_GetLabelId(UIScreen* screen, uint64_t widgetId, uint64_t* outLabelId)
{
    Widget* Target = NULL;
    UILabelledCapability* Self = NULL;
    Error Result = ResolveLabelled(screen, widgetId, &Target, &Self);
    if (Result.Code == ErrorCode_Success)
    {
        *outLabelId = Self->_labelId;
    }
    return Result;
}

Error UILabelled_GetElementId(UIScreen* screen, uint64_t widgetId, uint64_t* outElementId)
{
    Widget* Target = NULL;
    UILabelledCapability* Self = NULL;
    Error Result = ResolveLabelled(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    *outElementId = WIDGET_ID_INVALID;
    size_t Count = Widget_GetSubWidgetCount(Target);
    for (size_t Index = 0U; Index < Count; Index++)
    {
        Widget* Child = Widget_GetSubWidgetAt(Target, Index);
        if ((Child != NULL) && (Widget_GetId(Child) != Self->_labelId))
        {
            *outElementId = Widget_GetId(Child);
            break;
        }
    }
    return Error_CreateSuccess();
}

Error UILabelled_SetLayout(UIScreen* screen, uint64_t widgetId, UILabelledLayout layout)
{
    Widget* Target = NULL;
    UILabelledCapability* Self = NULL;
    Error Result = ResolveLabelled(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!IsLayoutValid(layout))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UILabelled_SetLayout: the layout is none of UILabelledLayout.");
    }
    if (Self->_layout != layout)
    {
        Self->_layout = layout;
        Widget_InvalidateMeasure(Target);
    }
    return Error_CreateSuccess();
}

Error UILabelled_SetLabelWidth(UIScreen* screen, uint64_t widgetId, bool hasLabelWidth, float width)
{
    Widget* Target = NULL;
    UILabelledCapability* Self = NULL;
    Error Result = ResolveLabelled(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (hasLabelWidth && (Math_IsNaNFloat(width) || (width < 0.0f) || Math_IsInfinityFloat(width)))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UILabelled_SetLabelWidth: the width must be a finite number >= 0.");
    }
    Self->_hasLabelWidth = hasLabelWidth;
    Self->_labelWidth = hasLabelWidth ? width : 0.0f;
    Widget_InvalidateMeasure(Target);
    return Error_CreateSuccess();
}


// Public functions: behaviour helpers.
void UILabelledCapability_SetSpacing(UILabelledCapability* self, Widget* widget, float spacing)
{
    float Spacing = ToLength(spacing);
    if (self->_spacing != Spacing)
    {
        self->_spacing = Spacing;
        Widget_InvalidateMeasure(widget);
    }
}

void UILabelled_SetLabel(UILabelledCapability* self, uint64_t labelId)
{
    self->_labelId = labelId;
}

Error UILabelled_Measure(Widget* widget, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    const UILabelledCapability* Self = GetLabelled(widget);
    Widget* Label = NULL;
    Widget* Element = NULL;
    FindParts(widget, Self, &Label, &Element);
    Vector2 LabelSize = { .x = 0.0f, .y = 0.0f };
    Error Result = MeasureLabel(Label, context, &LabelSize);
    return (Result.Code == ErrorCode_Success) ? MeasureWithLabelWidth(widget, context, availableWidth, ChooseLabelWidth(widget, Self, LabelSize.x), outSize)
        : Result;
}

Error UILabelled_Arrange(Widget* widget, const UILayoutContext* context)
{
    const UILabelledCapability* Self = GetLabelled(widget);
    float Scale = Widget_GetEffectiveScale(widget);
    Rectangle Pixels = UIScreen_GetWidgetPixelBox(Widget_GetScreen(widget), widget);
    Vector2 Box = { .x = Pixels.width / Scale, .y = Pixels.height / Scale };
    Widget* Label = NULL;
    Widget* Element = NULL;
    FindParts(widget, Self, &Label, &Element);
    Vector2 LabelSize = { .x = 0.0f, .y = 0.0f };
    Error Result = MeasureLabel(Label, context->Sizing, &LabelSize);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    bool IsAcross = Self->_layout == UILabelledLayout_Across;
    float LabelWidth = ChooseLabelWidth(widget, Self, LabelSize.x);
    float Spacing = ((Label != NULL) && (Element != NULL) && ((IsAcross ? LabelWidth : LabelSize.y) > 0.0f)) ? Self->_spacing : 0.0f;
    UIPlacement LabelPlacement;
    UIPlacement ElementPlacement;
    if (IsAcross)
    {
        LabelPlacement = UIPlacement_FromTopLeft(0.0f, Math_MaxFloat(0.0f, (Box.y - LabelSize.y) * 0.5f), LabelWidth, Math_MinFloat(LabelSize.y, Box.y));
        float ElementX = LabelWidth + Spacing;
        ElementPlacement = UIPlacement_FromTopLeft(ElementX, 0.0f, Math_MaxFloat(0.0f, Box.x - ElementX), Box.y);
    }
    else
    {
        LabelPlacement = UIPlacement_FromTopLeft(0.0f, 0.0f, Box.x, Math_MinFloat(LabelSize.y, Box.y));
        float ElementY = LabelSize.y + Spacing;
        ElementPlacement = UIPlacement_FromTopLeft(0.0f, ElementY, Box.x, Math_MaxFloat(0.0f, Box.y - ElementY));
    }
    Result = (Label != NULL) ? Widget_SetPlacement(Label, &LabelPlacement) : Error_CreateSuccess();
    if ((Result.Code == ErrorCode_Success) && (Element != NULL))
    {
        Result = Widget_SetPlacement(Element, &ElementPlacement);
    }
    return Result;
}

void UIFormCapability_SetMetrics(UIFormCapability* self, Widget* widget, Vector2 padding, float spacing)
{
    Vector2 Padding = { .x = ToLength(padding.x), .y = ToLength(padding.y) };
    float Spacing = ToLength(spacing);
    if ((self->_padding.x != Padding.x) || (self->_padding.y != Padding.y) || (self->_spacing != Spacing))
    {
        self->_padding = Padding;
        self->_spacing = Spacing;
        Widget_InvalidateMeasure(widget);
    }
}

Error UIForm_Measure(Widget* widget, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    const UIFormCapability* Self = GetForm(widget);
    float Shared = 0.0f;
    Error Result = MeasureSharedLabelWidth(widget, context, &Shared);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    float Available = (availableWidth < UI_MEASURE_UNBOUNDED_WIDTH) ? Math_MaxFloat(0.0f, availableWidth - (2.0f * Self->_padding.x)) : availableWidth;
    float Width = 0.0f;
    float Height = 0.0f;
    size_t Rows = 0U;
    size_t Count = Widget_GetSubWidgetCount(widget);
    for (size_t Index = 0U; Index < Count; Index++)
    {
        Widget* Child = Widget_GetSubWidgetAt(widget, Index);
        if ((Child == NULL) || !Widget_IsRendered(Child))
        {
            continue;
        }
        Vector2 Size = { .x = 0.0f, .y = 0.0f };
        Result = MeasureRow(Child, context, Available, Shared, &Size);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        Width = Math_MaxFloat(Width, Size.x);
        Height += Size.y;
        Rows++;
    }
    float Gaps = (Rows > 1U) ? (Self->_spacing * (float)(Rows - 1U)) : 0.0f;
    *outSize = (Vector2){ .x = Width + (2.0f * Self->_padding.x), .y = Height + Gaps + (2.0f * Self->_padding.y) };
    return Error_CreateSuccess();
}

Error UIForm_Arrange(Widget* widget, const UILayoutContext* context)
{
    UIFormCapability* Self = GetForm(widget);
    float Scale = Widget_GetEffectiveScale(widget);
    Rectangle Pixels = UIScreen_GetWidgetPixelBox(Widget_GetScreen(widget), widget);
    float Width = Math_MaxFloat(0.0f, (Pixels.width / Scale) - (2.0f * Self->_padding.x));
    float Shared = 0.0f;
    Error Result = MeasureSharedLabelWidth(widget, context->Sizing, &Shared);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Self->_labelWidth = Shared;
    float Y = Self->_padding.y;
    size_t Count = Widget_GetSubWidgetCount(widget);
    for (size_t Index = 0U; Index < Count; Index++)
    {
        Widget* Child = Widget_GetSubWidgetAt(widget, Index);
        if ((Child == NULL) || !Widget_IsRendered(Child))
        {
            continue;
        }
        UILabelledCapability* Labelled = IsSharingRow(Child) ? FindLabelled(Child) : NULL;
        if (Labelled != NULL)
        {
            float RowScale = Widget_GetScale(Child);
            float RowShared = Shared / ((RowScale > 0.0f) ? RowScale : 1.0f);
            if (!Labelled->_hasSharedLabelWidth || (Labelled->_sharedLabelWidth != RowShared))
            {
                Labelled->_hasSharedLabelWidth = true;
                Labelled->_sharedLabelWidth = RowShared;
                Widget_InvalidateMeasure(Child);
            }
        }
        Vector2 Size = { .x = 0.0f, .y = 0.0f };
        Result = MeasureRow(Child, context->Sizing, Width, Shared, &Size);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        UIPlacement Row = UIPlacement_FromTopLeft(Self->_padding.x, Y, Width, Size.y);
        Result = Widget_SetPlacement(Child, &Row);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        Y += Size.y + Self->_spacing;
    }
    return Error_CreateSuccess();
}
