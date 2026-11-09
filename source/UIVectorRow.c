#include "UIVectorRow.h"
#include "UIKit.h"
#include "UIKitInternal.h"
#include "UIPropertyBox.h"
#include "UIScreen.h"
#include "wr/WRMath.h"


// Static functions: small helpers.
static float ToLength(float value)
{
    return ((value > 0.0f) && !Math_IsInfinityFloat(value)) ? value : 0.0f;
}

static UIVectorRowCapability* GetCapability(void* widget)
{
    return &((UIVectorRowLookBase*)widget)->VectorRow;
}

static Error ResolveRow(UIScreen* screen, uint64_t widgetId, Widget** outWidget, UIVectorRowCapability** outSelf)
{
    UIKit* Kit = (screen != NULL) ? UIKit_FromScreen(screen) : NULL;
    Widget* Target = NULL;
    if ((Kit == NULL) || !UIScreen_TryGetWidgetById(screen, widgetId, &Target))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIVectorRow: the widget is gone, or its screen has no kit.");
    }
    UIVectorRowCapability* Self = Widget_GetCapability(Target, UIKit_GetCapabilityId(Kit, UIElementCapability_VectorRow));
    if (Self == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIVectorRow: the widget is not a row of fields.");
    }
    *outWidget = Target;
    *outSelf = Self;
    return Error_CreateSuccess();
}

/* An axis's field id, after checking the axis. */
static Error ResolveField(UIScreen* screen, uint64_t widgetId, size_t axis, uint64_t* outFieldId)
{
    Widget* Target = NULL;
    UIVectorRowCapability* Self = NULL;
    Error Result = ResolveRow(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (axis >= Self->_count)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIVectorRow: the axis is past the row's fields.");
    }
    *outFieldId = Self->_fieldIds[axis];
    return Error_CreateSuccess();
}

/* A field, when it is alive, still the row's and shown. */
static Widget* FindField(const Widget* widget, size_t axis)
{
    const UIVectorRowCapability* Self = &((const UIVectorRowLookBase*)(const void*)widget)->VectorRow;
    Widget* Field = NULL;
    if ((axis >= Self->_count) || !UIScreen_TryGetWidgetById(Widget_GetScreen(widget), Self->_fieldIds[axis], &Field)
        || (Widget_GetParent(Field) != widget) || !Widget_IsRendered(Field))
    {
        return NULL;
    }
    return Field;
}

/* How many fields show. */
static size_t CountShown(const Widget* widget)
{
    const UIVectorRowCapability* Self = &((const UIVectorRowLookBase*)(const void*)widget)->VectorRow;
    size_t Shown = 0U;
    for (size_t Axis = 0U; Axis < Self->_count; Axis++)
    {
        Shown += (FindField(widget, Axis) != NULL) ? 1U : 0U;
    }
    return Shown;
}

/* Each shown field's slot across a row @p width wide (its tag and its field): the width less the gaps, shared evenly. */
static float GetSlotWidth(const UIVectorRowCapability* self, float width, size_t shown)
{
    if (shown == 0U)
    {
        return 0.0f;
    }
    return Math_MaxFloat(0.0f, (width - (self->_spacing * (float)(shown - 1U))) / (float)shown);
}


// Static functions: the kind's behaviour, for the kit's composed widget types.
static void* ResolveCapability(void* widget)
{
    return GetCapability(widget);
}

static Error Behaviour_Construct(void* widget, void* args)
{
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIVectorRow: the arguments are required.");
    }
    const UIVectorRowOptions* Options = ((const UIVectorRowArgs*)args)->Options;
    if ((Options->Count < UI_VECTOR_ROW_MIN_COUNT) || (Options->Count > UI_VECTOR_ROW_MAX_COUNT))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIVectorRow: a row has two to four fields.");
    }
    Error Result = UIElement_ApplyOptions(widget, &Options->Element);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    UIVectorRowCapability* Self = GetCapability(widget);
    Self->_count = Options->Count;
    for (size_t Axis = 0U; Axis < UI_VECTOR_ROW_MAX_COUNT; Axis++)
    {
        Self->_fieldIds[Axis] = WIDGET_ID_INVALID;
    }
    Self->_spacing = 0.0f;
    Self->_tagWidth = 0.0f;
    return Error_CreateSuccess();
}

static Error Behaviour_Layout(void* widget, const UILayoutContext* context)
{
    return UIVectorRow_Arrange(widget, context);
}


// Fields.
static const UIKindCapability RowCapabilities[] = { { .Capability = UIElementCapability_VectorRow, .Resolve = ResolveCapability } };

static const UIKindBehaviour RowBehaviour =
{
    .Kind = UIElementKind_VectorRow,
    .Capabilities = RowCapabilities,
    .CapabilityCount = sizeof(RowCapabilities) / sizeof(RowCapabilities[0]),
    .BaseSize = sizeof(UIVectorRowLookBase),
    .IsFocusable = false,
    .NeedsEntering = false,
    .EntersItself = false,
    .Construct = Behaviour_Construct,
    .Layout = Behaviour_Layout
};


// Internal functions (declared in UIKitInternal.h).
const UIKindBehaviour* UIVectorRow_GetKindBehaviour(void)
{
    return &RowBehaviour;
}


// Public functions: helpers.
UIVectorRowOptions UIVectorRowOptions_Create(void)
{
    UIVectorRowOptions Options =
    {
        .Element = UIElementOptions_Create(),
        .Count = 3U,
        .Value = UIValueOptions_Create(),
        .Values = { 0.0, 0.0, 0.0, 0.0 },
        .Precision = UI_PROPERTY_BOX_DEFAULT_PRECISION,
        .Unit = NULL
    };
    Options.Value.Settings = UIValueSettings_Create();
    return Options;
}


// Public functions: calling code.
Error UIVectorRow_GetFieldId(UIScreen* screen, uint64_t widgetId, size_t axis, uint64_t* outFieldId)
{
    return ResolveField(screen, widgetId, axis, outFieldId);
}

Error UIVectorRow_SetValue(UIScreen* screen, uint64_t widgetId, size_t axis, double value)
{
    uint64_t FieldId = WIDGET_ID_INVALID;
    Error Result = ResolveField(screen, widgetId, axis, &FieldId);
    return (Result.Code == ErrorCode_Success) ? UIValue_Set(screen, FieldId, value) : Result;
}

Error UIVectorRow_GetValue(UIScreen* screen, uint64_t widgetId, size_t axis, double* outValue)
{
    uint64_t FieldId = WIDGET_ID_INVALID;
    Error Result = ResolveField(screen, widgetId, axis, &FieldId);
    return (Result.Code == ErrorCode_Success) ? UIValue_Get(screen, FieldId, outValue) : Result;
}

Error UIVectorRow_SetMixed(UIScreen* screen, uint64_t widgetId, size_t axis, bool isMixed)
{
    uint64_t FieldId = WIDGET_ID_INVALID;
    Error Result = ResolveField(screen, widgetId, axis, &FieldId);
    return (Result.Code == ErrorCode_Success) ? UIValue_SetMixed(screen, FieldId, isMixed) : Result;
}


// Public functions: behaviour helpers.
void UIVectorRowCapability_SetMetrics(UIVectorRowCapability* self, Widget* widget, float spacing, float tagWidth)
{
    float Spacing = ToLength(spacing);
    float TagWidth = ToLength(tagWidth);
    if ((self->_spacing != Spacing) || (self->_tagWidth != TagWidth))
    {
        self->_spacing = Spacing;
        self->_tagWidth = TagWidth;
        Widget_InvalidateMeasure(widget);
    }
}

Error UIVectorRow_Measure(Widget* widget, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)availableWidth;
    const UIVectorRowCapability* Self = GetCapability(widget);
    float Width = 0.0f;
    float Height = 0.0f;
    size_t Shown = 0U;
    for (size_t Axis = 0U; Axis < Self->_count; Axis++)
    {
        Widget* Field = FindField(widget, Axis);
        if (Field == NULL)
        {
            continue;
        }
        Vector2 Size = { .x = 0.0f, .y = 0.0f };
        Error Result = Widget_Measure(Field, context, UI_MEASURE_UNBOUNDED_WIDTH, &Size);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        Width += Self->_tagWidth + Size.x;
        Height = Math_MaxFloat(Height, Size.y);
        Shown++;
    }
    float Gaps = (Shown > 1U) ? (Self->_spacing * (float)(Shown - 1U)) : 0.0f;
    *outSize = (Vector2){ .x = Width + Gaps, .y = Height };
    return Error_CreateSuccess();
}

Error UIVectorRow_Arrange(Widget* widget, const UILayoutContext* context)
{
    (void)context;
    const UIVectorRowCapability* Self = GetCapability(widget);
    float Scale = Widget_GetEffectiveScale(widget);
    Rectangle Pixels = UIScreen_GetWidgetPixelBox(Widget_GetScreen(widget), widget);
    Vector2 Area = { .x = Pixels.width / Scale, .y = Pixels.height / Scale };
    size_t Shown = CountShown(widget);
    if (Shown == 0U)
    {
        return Error_CreateSuccess();
    }
    float Each = GetSlotWidth(Self, Area.x, Shown);
    float FieldWidth = Math_MaxFloat(0.0f, Each - Self->_tagWidth);
    float X = 0.0f;
    for (size_t Axis = 0U; Axis < Self->_count; Axis++)
    {
        Widget* Field = FindField(widget, Axis);
        if (Field == NULL)
        {
            continue;
        }
        UIPlacement Placement = UIPlacement_FromTopLeft(X + Math_MinFloat(Self->_tagWidth, Each), 0.0f, FieldWidth, Area.y);
        Error Result = Widget_SetPlacement(Field, &Placement);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        X += Each + Self->_spacing;
    }
    return Error_CreateSuccess();
}

bool UIVectorRow_GetTagBox(const Widget* widget, size_t axis, Rectangle* outBox)
{
    const UIVectorRowCapability* Self = &((const UIVectorRowLookBase*)(const void*)widget)->VectorRow;
    Widget* Field = FindField(widget, axis);
    if (Field == NULL)
    {
        return false;
    }
    const UIScreen* Screen = Widget_GetScreen(widget);
    float Scale = Widget_GetEffectiveScale(widget);
    Rectangle Row = UIScreen_GetWidgetPixelBox(Screen, widget);
    Rectangle Box = UIScreen_GetWidgetPixelBox(Screen, Field);
    // The tag's room in its own slot: a row too narrow for its tags gives each the slot's width, never the previous field's.
    float Width = Math_MinFloat(Self->_tagWidth, GetSlotWidth(Self, Row.width / Scale, CountShown(widget)));
    *outBox = (Rectangle){ .x = ((Box.x - Row.x) / Scale) - Width, .y = (Box.y - Row.y) / Scale, .width = Width, .height = Box.height / Scale };
    return true;
}

Error UIVectorRow_BindFields(UIScreen* screen, uint64_t widgetId, const uint64_t* fieldIds)
{
    Widget* Target = NULL;
    UIVectorRowCapability* Self = NULL;
    Error Result = ResolveRow(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    size_t Count = Self->_count;
    for (size_t Axis = 0U; Axis < Count; Axis++)
    {
        Widget* Field = NULL;
        UIValueCapability* Value = NULL;
        Result = UIValue_Resolve(screen, fieldIds[Axis], &Field, &Value);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        UIValueCapability_SetEventTarget(Value, widgetId, true, (int32_t)Axis);
    }
    for (size_t Axis = 0U; Axis < Count; Axis++)
    {
        Self->_fieldIds[Axis] = fieldIds[Axis];
    }
    return Error_CreateSuccess();
}
