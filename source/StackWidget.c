#include "StackWidget.h"
#include "UIScreen.h"
#include "UIWidgetFactory.h"
#include "wr/WRMath.h"


// Types.
/* A child's sizes along the stack's axes, in UI pixels. */
typedef struct ChildSizesStruct
{
    float Main;
    float Cross;
} ChildSizes;


// Static functions.
static bool IsRow(const UIStackLayout* layout)
{
    return layout->Direction == UIStackDirection_Row;
}

/* A length less what is taken off it, never below zero; an unbounded one stays unbounded. */
static float Remaining(float length, float taken)
{
    return Math_MaxFloat(0.0f, length - taken);
}

/* A stack's own box in UI pixels, kept to what its children can share out (a NaN from a box of no use counts as all of it). */
static float LimitOwnLength(float length)
{
    return (length < UI_LAYOUT_MAX_LENGTH) ? length : UI_LAYOUT_MAX_LENGTH;
}

static float GetGaps(const UIStackLayout* layout, size_t shown)
{
    return (shown > 1U) ? ((float)(shown - 1U) * layout->Spacing) : 0.0f;
}

/* What a child may take along the main axis: one that fills its share of what the others left, any other what the children
 * before it left. @p usedSoFar counts the sizes of the children before it that do not fill, @p placed all of them. */
static float GetAvailableMain(const UIStackLayout* layout, const UIStackHint* hint, float main, float left, float weights, float usedSoFar,
    size_t placed)
{
    if (hint->Mode == UIStackSizeMode_Fill)
    {
        return (weights > 0.0f) ? ((left * hint->Weight) / weights) : 0.0f;
    }
    return Remaining(main, usedSoFar + ((float)placed * layout->Spacing));
}

/* The child's sizes by its hint. @p availableMain is what a row's child may take in width, @p cross what a column's child
 * has in width: the width it is measured at. */
static Error MeasureChild(Widget* child, const UIStackHint* hint, bool isRow, const UIMeasureContext* context, float availableMain,
    float cross, ChildSizes* outSizes)
{
    bool UsesCrossSize = hint->HasCrossSize && (hint->Align != UIStackAlign_Stretch);
    float MeasureWidth = 0.0f;
    if (isRow)
    {
        MeasureWidth = (hint->Mode == UIStackSizeMode_Fixed) ? hint->Size : availableMain;
    }
    else
    {
        MeasureWidth = UsesCrossSize ? hint->CrossSize : cross;
    }
    Vector2 Preferred = { .x = 0.0f, .y = 0.0f };
    Error Result = Widget_Measure(child, context, MeasureWidth, &Preferred);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    float PreferredMain = isRow ? Preferred.x : Preferred.y;
    outSizes->Main = (hint->Mode == UIStackSizeMode_Fixed) ? hint->Size : PreferredMain;
    outSizes->Cross = UsesCrossSize ? hint->CrossSize : (isRow ? Preferred.y : Preferred.x);
    return Error_CreateSuccess();
}

/* What the fixed and content children take along the main axis, spacing aside, the total of the weights and how many are
 * shown. */
static Error SumChildren(const UIStackLayout* layout, Widget* widget, const UIMeasureContext* context, float main, float cross,
    float* outUsed, float* outWeights, size_t* outShown)
{
    float Used = 0.0f;
    float Weights = 0.0f;
    size_t Shown = 0U;
    size_t Count = Widget_GetSubWidgetCount(widget);
    for (size_t Index = 0U; Index < Count; Index++)
    {
        Widget* Child = Widget_GetSubWidgetAt(widget, Index);
        if (!Widget_IsRendered(Child))
        {
            continue;
        }
        UIStackHint Hint = Widget_GetStackHint(Child);
        if (Hint.Mode == UIStackSizeMode_Fill)
        {
            Weights += Hint.Weight;
        }
        else
        {
            ChildSizes Sizes;
            float Available = GetAvailableMain(layout, &Hint, main, 0.0f, 0.0f, Used, Shown);
            Error Result = MeasureChild(Child, &Hint, IsRow(layout), context, Available, cross, &Sizes);
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }
            Used += Sizes.Main;
        }
        Shown++;
    }
    *outUsed = Used;
    *outWeights = Weights;
    *outShown = Shown;
    return Error_CreateSuccess();
}

/* Where a child's cross size starts within the cross length, and how long it ends up. */
static void AlignAcross(UIStackAlign align, float cross, float childCross, float* outStart, float* outLength)
{
    switch (align)
    {
        case UIStackAlign_Start:
            *outStart = 0.0f;
            *outLength = childCross;
            return;
        case UIStackAlign_Center:
            *outStart = (cross - childCross) * 0.5f;
            *outLength = childCross;
            return;
        case UIStackAlign_End:
            *outStart = cross - childCross;
            *outLength = childCross;
            return;
        case UIStackAlign_Stretch:
            *outStart = 0.0f;
            *outLength = cross;
            return;
    }
    *outStart = 0.0f;
    *outLength = childCross;
}


// Static functions: the widget hooks.
static Error StackWidget_ConstructVT(void* widget, void* args)
{
    StackWidget* Stack = widget;
    const UIStackOptions* Options = args;
    if (Options == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "StackWidget: the options are required.");
    }
    if (!UIStackLayout_IsValid(&Options->Layout))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "StackWidget: a known direction, and padding and spacing within [0, UI_LAYOUT_MAX_LENGTH], are required.");
    }
    Stack->_layout = Options->Layout;
    Error Result = UIElement_ApplyOptions(&Stack->Base, &Options->Element);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return Widget_SetIsInputEnabled(&Stack->Base, false); // Only the flag: the widget is not initialized yet.
}

static Error StackWidget_LayoutVT(void* self, const UILayoutContext* context)
{
    StackWidget* Stack = self;
    return UIStackLayout_Arrange(&Stack->_layout, &Stack->Base, context);
}

static Error StackWidget_MeasureVT(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    StackWidget* Stack = self;
    return UIStackLayout_Measure(&Stack->_layout, &Stack->Base, context, availableWidth, outSize);
}


// Fields.
static const WidgetVTable StackWidgetVTable =
{
    .Layout = StackWidget_LayoutVT,
    .Measure = StackWidget_MeasureVT
};


// Public functions.
bool UIStackLayout_IsValid(const UIStackLayout* layout)
{
    bool IsDirectionValid = (layout->Direction == UIStackDirection_Column) || (layout->Direction == UIStackDirection_Row);
    return IsDirectionValid && UILayoutLength_IsValid(layout->Spacing) && UILayoutLength_IsValid(layout->Padding.Left)
        && UILayoutLength_IsValid(layout->Padding.Top) && UILayoutLength_IsValid(layout->Padding.Right)
        && UILayoutLength_IsValid(layout->Padding.Bottom);
}

Error UIStackLayout_Measure(const UIStackLayout* layout, Widget* widget, const UIMeasureContext* context, float availableWidth,
    Vector2* outSize)
{
    float PaddingWidth = layout->Padding.Left + layout->Padding.Right;
    float PaddingHeight = layout->Padding.Top + layout->Padding.Bottom;
    float ContentWidth = Remaining(availableWidth, PaddingWidth);
    bool Row = IsRow(layout);
    float Main = Row ? ContentWidth : UI_MEASURE_UNBOUNDED_WIDTH;
    float Cross = Row ? UI_MEASURE_UNBOUNDED_WIDTH : ContentWidth;

    // As when it is arranged: what fills is asked about its share of what the others leave, so the answer is the one it will get.
    float Used = 0.0f;
    float Weights = 0.0f;
    size_t Shown = 0U;
    Error Result = SumChildren(layout, widget, context, Main, Cross, &Used, &Weights, &Shown);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    float Gaps = GetGaps(layout, Shown);
    float Left = Remaining(Main, Used + Gaps);

    float Total = 0.0f;
    float CrossLargest = 0.0f;
    float UsedSoFar = 0.0f;
    size_t Placed = 0U;
    size_t Count = Widget_GetSubWidgetCount(widget);
    for (size_t Index = 0U; Index < Count; Index++)
    {
        Widget* Child = Widget_GetSubWidgetAt(widget, Index);
        if (!Widget_IsRendered(Child))
        {
            continue;
        }
        UIStackHint Hint = Widget_GetStackHint(Child);
        ChildSizes Sizes;
        float Available = GetAvailableMain(layout, &Hint, Main, Left, Weights, UsedSoFar, Placed);
        Result = MeasureChild(Child, &Hint, Row, context, Available, Cross, &Sizes);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        Total += Sizes.Main;
        CrossLargest = Math_MaxFloat(CrossLargest, Sizes.Cross);
        if (Hint.Mode != UIStackSizeMode_Fill)
        {
            UsedSoFar += Sizes.Main;
        }
        Placed++;
    }
    outSize->x = (Row ? (Total + Gaps) : CrossLargest) + PaddingWidth;
    outSize->y = (Row ? CrossLargest : (Total + Gaps)) + PaddingHeight;
    return Error_CreateSuccess();
}

Error UIStackLayout_Arrange(const UIStackLayout* layout, Widget* widget, const UILayoutContext* context)
{
    // Its own UI pixels: padding, spacing and its children's hints and placements all count in its effective scale.
    float Scale = Widget_GetEffectiveScale(widget);
    Vector2 OwnSize = Widget_GetSize(widget);
    float OwnWidth = LimitOwnLength((OwnSize.x * context->ParentAbsoluteSize.x * context->SurfaceSizePixels.x) / Scale);
    float OwnHeight = LimitOwnLength((OwnSize.y * context->ParentAbsoluteSize.y * context->SurfaceSizePixels.y) / Scale);
    float ContentWidth = Remaining(OwnWidth, layout->Padding.Left + layout->Padding.Right);
    float ContentHeight = Remaining(OwnHeight, layout->Padding.Top + layout->Padding.Bottom);
    bool Row = IsRow(layout);
    float Main = Row ? ContentWidth : ContentHeight;
    float Cross = Row ? ContentHeight : ContentWidth;

    float Used = 0.0f;
    float Weights = 0.0f;
    size_t Shown = 0U;
    Error Result = SumChildren(layout, widget, context->Sizing, Main, Cross, &Used, &Weights, &Shown);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    float Left = Remaining(Main, Used + GetGaps(layout, Shown));

    float Cursor = 0.0f;       // Along the main axis, from the content box's start.
    float UsedSoFar = 0.0f;    // As SumChildren counts, so a child is measured at the same width: the cache answers.
    size_t Placed = 0U;
    size_t Count = Widget_GetSubWidgetCount(widget);
    for (size_t Index = 0U; Index < Count; Index++)
    {
        Widget* Child = Widget_GetSubWidgetAt(widget, Index);
        if (!Widget_IsRendered(Child))
        {
            UIPlacement Hidden = UIPlacement_FromTopLeft(0.0f, 0.0f, 0.0f, 0.0f);
            Result = Widget_SetPlacement(Child, &Hidden);
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }
            continue;
        }
        UIStackHint Hint = Widget_GetStackHint(Child);
        bool IsFilling = Hint.Mode == UIStackSizeMode_Fill;
        float Available = GetAvailableMain(layout, &Hint, Main, Left, Weights, UsedSoFar, Placed);
        ChildSizes Sizes;
        Result = MeasureChild(Child, &Hint, Row, context->Sizing, Available, Cross, &Sizes);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        float ChildMain = IsFilling ? Available : Sizes.Main;
        float CrossStart = 0.0f;
        float CrossLength = 0.0f;
        AlignAcross(Hint.Align, Cross, Sizes.Cross, &CrossStart, &CrossLength);

        UIPlacement Placement = Row
            ? UIPlacement_FromTopLeft(layout->Padding.Left + Cursor, layout->Padding.Top + CrossStart, ChildMain, CrossLength)
            : UIPlacement_FromTopLeft(layout->Padding.Left + CrossStart, layout->Padding.Top + Cursor, CrossLength, ChildMain);
        Result = Widget_SetPlacement(Child, &Placement);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        Cursor += ChildMain + layout->Spacing;
        if (!IsFilling)
        {
            UsedSoFar += Sizes.Main;
        }
        Placed++;
    }
    return Error_CreateSuccess();
}

Error StackWidget_RegisterType(UIWidgetFactory* factory, uint64_t* outTypeId)
{
    WidgetTypeDescriptor Descriptor =
    {
        .Name = u8"Stack",
        .StructSize = sizeof(StackWidget),
        .VTable = &StackWidgetVTable,
        .Constructor = StackWidget_ConstructVT,
        .Capabilities = NULL,
        .CapabilityCount = 0U,
        .Properties = NULL,
        .PropertyCount = 0U,
        .IsFocusable = false
    };
    return UIWidgetFactory_RegisterType(factory, &Descriptor, outTypeId);
}

Error StackWidget_SetChildHint(UIScreen* screen, uint64_t childId, const UIStackHint* hint)
{
    Widget* Child = NULL;
    if ((screen == NULL) || (hint == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "StackWidget_SetChildHint: screen and hint must not be NULL.");
    }
    if (!UIScreen_TryGetWidgetById(screen, childId, &Child))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "StackWidget_SetChildHint: the child is gone.");
    }
    return Widget_SetStackHint(Child, hint);
}
