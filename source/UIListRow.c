#include "UIListRow.h"
#include "UIKit.h"
#include "UIScreen.h"
#include "UIWidgetFactory.h"
#include "wr/WRMath.h"


// Types.
/* What a row is made with. */
typedef struct ListRowArgsStruct
{
    UIKit* Kit;
    const UIListRowParts* Parts;
    const unsigned char* Variant;
} ListRowArgs;

/* One row of a list or a tree: what it shows, how it stands, and the views it draws its text and icons with. */
typedef struct ListRowStruct
{
    Widget Base;
    UIElementLook Look;
    const UIListRowParts* Parts;
    UIElementText Text;
    UITextContent Content;
    UIElementIconView IconView;
    UIElementIcon Icon;
    UIElementIconView CellViews[UI_LIST_MAX_CELLS];
    UIElementIcon CellIcons[UI_LIST_MAX_CELLS];
    bool Cells[UI_LIST_MAX_CELLS];
    size_t CellCount;
    bool IsBound;
    bool IsDimmed;
    uint32_t Depth;
    bool HasChildren;
    bool IsExpanded;
    UIListRowView View;
    UIListRowGeometry Geometry;
} ListRow;


// Fields.
static const UIListRowParts ListParts =
{
    .Kind = UIElementKind_List, .Row = UIListPart_Row, .Current = UIListPart_Current, .Text = UIListPart_Text, .Cell = UIListPart_Cell,
    .CellText = UIListPart_CellText, .Insertion = UIListPart_Insertion, .Expander = -1
};

static const UIListRowParts TreeParts =
{
    .Kind = UIElementKind_Tree, .Row = UITreePart_Row, .Current = UITreePart_Current, .Text = UITreePart_Text, .Cell = UITreePart_Cell,
    .CellText = UITreePart_CellText, .Insertion = UITreePart_Insertion, .Expander = UITreePart_Expander
};


// Static functions.
static float RoundToPixel(float value)
{
    return Math_FloorFloat(value + 0.5f);
}

static Error KeepFirst(Error first, Error later)
{
    if (first.Code == ErrorCode_Success)
    {
        return later;
    }
    Error_Deconstruct(&later);
    return first;
}

static ListRow* ResolveRow(UIScreen* screen, uint64_t rowId)
{
    Widget* Found = NULL;
    return UIScreen_TryGetWidgetById(screen, rowId, &Found) ? (ListRow*)(void*)Found : NULL;
}

/* The state the row's box shows: its owner disabled, selected, hovered, or normal. */
static UIElementState GetRowState(const ListRow* row)
{
    return UIElementState_Choose(row->View.IsEnabled, false, false, false, row->View.IsSelected, row->View.IsHovered, false);
}

/* Draws a box part as it looks in one state; a part the theme leaves out draws a plain box. */
static Error DrawPartInState(const UIThemeStyle* style, int32_t part, UIElementState state, UIRenderContext* context, Rectangle box,
    float scale)
{
    const UIBoxStyle* Box = UIThemeStyle_GetBox(style, part);
    if (Box == NULL)
    {
        return UIBoxStyle_DrawFallback(context, box, scale);
    }
    UIBoxPaint Paint = UIBoxStyle_Resolve(Box, state);
    return UIBoxStyle_Draw(Box, context, box, &Paint, scale);
}

static Color ResolveColor(const UIThemeStyle* style, int32_t part, UIElementState state)
{
    const UIStateColors* Colors = UIThemeStyle_GetColors(style, part);
    return (Colors != NULL) ? UIStateColors_Resolve(Colors, state, WHITE) : WHITE;
}

static Error Row_Construct(void* widget, void* args)
{
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIListRow: a row's arguments are required.");
    }
    const ListRowArgs* Arguments = args;
    ListRow* Row = widget;
    Error Result = UIElementLook_Construct(&Row->Look, Arguments->Parts->Kind, Arguments->Variant);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = UITextContent_Construct(&Row->Content, NULL);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    TextComponentFactory* Factory = UIKit_GetTextFactory(Arguments->Kit);
    AssetManager* Assets = UIKit_GetAssets(Arguments->Kit);
    Logger* Log = UIKit_GetLogger(Arguments->Kit);
    Result = UIElementText_Construct(&Row->Text, Factory, Assets, Log);
    if (Result.Code != ErrorCode_Success)
    {
        UITextContent_Deconstruct(&Row->Content);
        return Result;
    }
    Result = UIElementIconView_Construct(&Row->IconView, Factory, Assets, Log);
    bool IsIconViewMade = Result.Code == ErrorCode_Success;
    size_t MadeCells = 0U;
    while ((Result.Code == ErrorCode_Success) && (MadeCells < UI_LIST_MAX_CELLS))
    {
        Result = UIElementIconView_Construct(&Row->CellViews[MadeCells], Factory, Assets, Log);
        MadeCells += (Result.Code == ErrorCode_Success) ? 1U : 0U;
    }
    if (Result.Code != ErrorCode_Success)
    {
        Error Cleanup = Error_CreateSuccess();
        for (size_t Index = 0U; Index < MadeCells; Index++)
        {
            Cleanup = KeepFirst(Cleanup, UIElementIconView_Deconstruct(&Row->CellViews[Index]));
        }
        if (IsIconViewMade)
        {
            Cleanup = KeepFirst(Cleanup, UIElementIconView_Deconstruct(&Row->IconView));
        }
        Cleanup = KeepFirst(Cleanup, UIElementText_Deconstruct(&Row->Text));
        Error_Deconstruct(&Cleanup);
        UITextContent_Deconstruct(&Row->Content);
        return Result;
    }
    Row->Parts = Arguments->Parts;
    Row->Icon = UIElementIcon_None();
    for (size_t Index = 0U; Index < UI_LIST_MAX_CELLS; Index++)
    {
        Row->CellIcons[Index] = UIElementIcon_None();
        Row->Cells[Index] = false;
    }
    Row->CellCount = 0U;
    Row->IsBound = false;
    Row->IsDimmed = false;
    Row->Depth = 0U;
    Row->HasChildren = false;
    Row->IsExpanded = false;
    Row->View = (UIListRowView){ .IsEnabled = true, .HoveredCell = -1, .PressedCell = -1, .Insertion = UIListRowInsertion_None };
    Row->Geometry = (UIListRowGeometry){ .RowPadding = 0.0f, .CellSize = 0.0f, .InsertionThickness = 0.0f, .Indent = 0.0f, .HasExpanderColumn = false };
    // It only shows: its owner takes the pointer.
    return Widget_SetIsInputEnabled(widget, false); // Only the flag: not initialized yet.
}

static Error Row_OnDeconstruct(void* self)
{
    ListRow* Row = self;
    Error Result = Error_CreateSuccess();
    for (size_t Index = 0U; Index < UI_LIST_MAX_CELLS; Index++)
    {
        Result = KeepFirst(Result, UIElementIconView_Deconstruct(&Row->CellViews[Index]));
    }
    Result = KeepFirst(Result, UIElementIconView_Deconstruct(&Row->IconView));
    Result = KeepFirst(Result, UIElementText_Deconstruct(&Row->Text));
    UITextContent_Deconstruct(&Row->Content);
    return Result;
}

/* Brings the text and the icons in line with what the row shows. */
static Error Row_Refresh(ListRow* row)
{
    Error Result = UIElementText_Refresh(&row->Text, &row->Base, &row->Content);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIElementIconView_Refresh(&row->IconView, &row->Base, &row->Icon);
    }
    for (size_t Index = 0U; (Index < row->CellCount) && (Result.Code == ErrorCode_Success); Index++)
    {
        Result = UIElementIconView_Refresh(&row->CellViews[Index], &row->Base, &row->CellIcons[Index]);
    }
    return Result;
}

static Error Row_Update(void* self, ProgramTime time)
{
    ListRow* Row = self;
    UIElementLook_Update(&Row->Look, Widget_GetScreen(&Row->Base), GetRowState(Row), time.PassedTime);
    Error Result = Row_Refresh(Row);
    return (Result.Code == ErrorCode_Success) ? UIElementIconView_Update(&Row->IconView, time) : Result;
}

static Error Row_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)context;
    (void)availableWidth;
    ListRow* Row = self;
    Error Result = Row_Refresh(Row);
    const UIScreen* Screen = Widget_GetScreen(&Row->Base);
    Vector2 Text = UIElementText_GetPixelSize(&Row->Text, Screen);
    float Scale = Widget_GetEffectiveScale(&Row->Base);
    outSize->x = (Text.x / Scale) + (2.0f * Row->Geometry.RowPadding);
    outSize->y = Text.y / Scale;
    return Result;
}

/* The cells at the row's right, each a box in its state (on is selected) with its letter. */
static Error RenderCells(const ListRow* row, const UIThemeStyle* style, UIRenderContext* context, Rectangle pixels, float scale)
{
    const UIScreen* Screen = Widget_GetScreen(&row->Base);
    float RowWidth = pixels.width / scale;
    float RowHeight = pixels.height / scale;
    Error Result = Error_CreateSuccess();
    for (size_t Index = 0U; (Index < row->CellCount) && (Result.Code == ErrorCode_Success); Index++)
    {
        Rectangle Box = UIListRow_GetCellBox(&row->Geometry, RowWidth, RowHeight, row->CellCount, Index);
        Rectangle Pixels = { .x = pixels.x + RoundToPixel(Box.x * scale), .y = pixels.y + RoundToPixel(Box.y * scale),
            .width = RoundToPixel(Box.width * scale), .height = RoundToPixel(Box.height * scale) };
        if (UIElement_IsBoxEmpty(Pixels))
        {
            continue;
        }
        bool IsHovered = row->View.HoveredCell == (int32_t)Index;
        bool IsPressed = (row->View.PressedCell == (int32_t)Index) && IsHovered;
        UIElementState State = UIElementState_Choose(row->View.IsEnabled, IsPressed, false, false, row->Cells[Index], IsHovered, false);
        Result = DrawPartInState(style, row->Parts->Cell, State, context, Pixels, scale);
        if ((Result.Code == ErrorCode_Success) && UIElementIconView_IsShowing(&row->CellViews[Index]))
        {
            Vector2 Letter = UIElementIconView_GetPixelSize(&row->CellViews[Index], Screen);
            Result = UIElementIconView_Draw(&row->CellViews[Index], context, Pixels.x + RoundToPixel((Pixels.width - Letter.x) * 0.5f),
                Pixels.y + RoundToPixel((Pixels.height - Letter.y) * 0.5f), ResolveColor(style, row->Parts->CellText, State));
        }
    }
    return Result;
}

/* The line where a drop would go: along the row's top or bottom edge, or for a drop into the row the same line around it (its four
 * edges, so the row's text still shows). */
static Error RenderInsertion(const ListRow* row, const UIThemeStyle* style, UIRenderContext* context, Rectangle pixels, float scale)
{
    float Thickness = Math_MinFloat(Math_MaxFloat(1.0f, RoundToPixel(row->Geometry.InsertionThickness * scale)), pixels.height);
    Rectangle Top = { .x = pixels.x, .y = pixels.y, .width = pixels.width, .height = Thickness };
    Rectangle Bottom = { .x = pixels.x, .y = (pixels.y + pixels.height) - Thickness, .width = pixels.width, .height = Thickness };
    switch (row->View.Insertion)
    {
        case UIListRowInsertion_None:
            return Error_CreateSuccess();
        case UIListRowInsertion_Above:
            return DrawPartInState(style, row->Parts->Insertion, UIElementState_Normal, context, Top, scale);
        case UIListRowInsertion_Below:
            return DrawPartInState(style, row->Parts->Insertion, UIElementState_Normal, context, Bottom, scale);
        case UIListRowInsertion_Into:
            break;
    }
    Rectangle Left = { .x = pixels.x, .y = pixels.y, .width = Math_MinFloat(Thickness, pixels.width), .height = pixels.height };
    Rectangle Right = { .x = (pixels.x + pixels.width) - Left.width, .y = pixels.y, .width = Left.width, .height = pixels.height };
    Rectangle Edges[] = { Top, Bottom, Left, Right };
    Error Result = Error_CreateSuccess();
    for (size_t Index = 0U; (Index < (sizeof(Edges) / sizeof(Edges[0]))) && (Result.Code == ErrorCode_Success); Index++)
    {
        Result = DrawPartInState(style, row->Parts->Insertion, UIElementState_Normal, context, Edges[Index], scale);
    }
    return Result;
}

static Error Row_Render(void* self, UIRenderContext* context)
{
    ListRow* Row = self;
    Rectangle Pixels = UIRenderContext_GetPixelBox(context);
    if (!Row->IsBound || UIElement_IsBoxEmpty(Pixels))
    {
        return Error_CreateSuccess();
    }
    const UIScreen* Screen = Widget_GetScreen(&Row->Base);
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Row->Look, Screen);
    float Scale = Widget_GetEffectiveScale(&Row->Base);
    Error Result = UIElementLook_DrawBox(&Row->Look, Style, Row->Parts->Row, context, Pixels, Scale);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    UIElementState TextState = Row->IsDimmed ? UIElementState_Disabled : GetRowState(Row);
    Color TextColor = Row->IsDimmed ? ResolveColor(Style, Row->Parts->Text, UIElementState_Disabled)
        : UIElementLook_BlendColors(&Row->Look, UIThemeStyle_GetColors(Style, Row->Parts->Text), WHITE);
    float X = Pixels.x + RoundToPixel(UIListRow_GetContentStart(&Row->Geometry, Row->Depth) * Scale);
    if (Row->Geometry.HasExpanderColumn)
    {
        if (Row->HasChildren && (Row->Parts->Expander >= 0))
        {
            Rectangle Box = UIListRow_GetExpanderBox(&Row->Geometry, Row->Depth, Pixels.height / Scale);
            float Side = Math_MaxFloat(1.0f, RoundToPixel(Math_MinFloat(Box.width, Box.height) * 0.5f * Scale));
            Rectangle Arrow = { .x = Pixels.x + RoundToPixel((Box.x + (Box.width * 0.5f)) * Scale - (Side * 0.5f)),
                .y = Pixels.y + RoundToPixel((Pixels.height - Side) * 0.5f), .width = Side, .height = Side };
            UIElementState ArrowState = Row->View.IsExpanderHovered ? UIElementState_Hovered : TextState;
            UIElement_DrawArrow(context, Arrow, Row->IsExpanded ? UIArrowDirection_Down : UIArrowDirection_Right,
                ResolveColor(Style, Row->Parts->Expander, ArrowState));
        }
        X += RoundToPixel(Row->Geometry.Indent * Scale);
    }
    float Gap = RoundToPixel(Row->Geometry.RowPadding * Scale);
    if (UIElementIconView_IsShowing(&Row->IconView))
    {
        Vector2 Icon = UIElementIconView_GetPixelSize(&Row->IconView, Screen);
        Result = UIElementIconView_Draw(&Row->IconView, context, X, Pixels.y + RoundToPixel((Pixels.height - Icon.y) * 0.5f), TextColor);
        X += Icon.x + Gap;
    }
    // The text, cut where the cells begin.
    float TextEnd = (Pixels.x + Pixels.width) - Gap;
    if (Row->CellCount > 0U)
    {
        Rectangle First = UIListRow_GetCellBox(&Row->Geometry, Pixels.width / Scale, Pixels.height / Scale, Row->CellCount, 0U);
        TextEnd = Pixels.x + RoundToPixel(First.x * Scale) - Gap;
    }
    if ((Result.Code == ErrorCode_Success) && (TextEnd > X))
    {
        Vector2 Text = UIElementText_GetPixelSize(&Row->Text, Screen);
        Rectangle TextArea = { .x = X, .y = Pixels.y, .width = TextEnd - X, .height = Pixels.height };
        RenderContext_PushClip(UIRenderContext_GetRenderContext(context), TextArea);
        Result = UIElementText_Draw(&Row->Text, context, X, Pixels.y + RoundToPixel((Pixels.height - Text.y) * 0.5f), TextColor);
        RenderContext_PopClip(UIRenderContext_GetRenderContext(context));
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = RenderCells(Row, Style, context, Pixels, Scale);
    }
    if ((Result.Code == ErrorCode_Success) && Row->View.IsCurrentShown)
    {
        Result = DrawPartInState(Style, Row->Parts->Current, Row->View.IsOwnerEntered ? UIElementState_Entered : UIElementState_Focused,
            context, Pixels, Scale);
    }
    return (Result.Code == ErrorCode_Success) ? RenderInsertion(Row, Style, context, Pixels, Scale) : Result;
}


// Fields.
static const WidgetVTable RowVTable =
{
    .OnDeconstruct = Row_OnDeconstruct,
    .Update = Row_Update,
    .Render = Row_Render,
    .Measure = Row_Measure
};


// Internal functions (declared in UIListRow.h).
const UIListRowParts* UIListRow_GetListParts(void)
{
    return &ListParts;
}

const UIListRowParts* UIListRow_GetTreeParts(void)
{
    return &TreeParts;
}

Error UIListRow_RegisterType(UIKit* kit, UIWidgetFactory* factory, uint64_t* outTypeId)
{
    (void)kit;
    WidgetTypeDescriptor Row = { .Name = u8"ListRow", .StructSize = sizeof(ListRow), .VTable = &RowVTable, .Constructor = Row_Construct };
    return UIWidgetFactory_RegisterType(factory, &Row, outTypeId);
}

Error UIListRow_Create(UIScreen* screen, UIKit* kit, uint64_t parentId, const UIListRowParts* parts, const unsigned char* variant,
    uint64_t* outId)
{
    *outId = WIDGET_ID_INVALID;
    ListRowArgs Arguments = { .Kit = kit, .Parts = parts, .Variant = variant };
    Widget* Made = NULL;
    Error Result = UIWidgetFactory_CreateWidget(kit->_factory, screen, kit->_listRowType, &Arguments, &Made);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    uint64_t RowId = Widget_GetId(Made);
    Result = Widget_InitializeWidget(Made);
    Widget* Parent = NULL;
    if ((Result.Code == ErrorCode_Success) && UIScreen_TryGetWidgetById(screen, parentId, &Parent) && UIScreen_TryGetWidgetById(screen, RowId, &Made))
    {
        Result = Widget_AddSubWidget(Parent, Made);
    }
    else if (Result.Code == ErrorCode_Success)
    {
        Result = Error_Construct2(ErrorCode_InvalidOperation, "UIListRow: the rows' box is gone.");
    }
    if (Result.Code != ErrorCode_Success)
    {
        if (UIScreen_TryGetWidgetById(screen, RowId, &Made))
        {
            Error Cleanup = Widget_Destroy(Made);
            Error_Deconstruct(&Cleanup);
        }
        return Result;
    }
    *outId = RowId;
    return Error_CreateSuccess();
}

Error UIListRow_Bind(UIScreen* screen, uint64_t rowId, const UIListRowContent* content)
{
    ListRow* Row = ResolveRow(screen, rowId);
    if (Row == NULL)
    {
        return Error_CreateSuccess();
    }
    Error Result = UITextContent_Set(&Row->Content, (content->Text != NULL) ? content->Text : (const unsigned char*)u8"", NULL);
    if (Result.Code != ErrorCode_Success)
    {
        // Text that is not valid UTF-8 shows nothing.
        Error_Deconstruct(&Result);
        Result = UITextContent_Set(&Row->Content, (const unsigned char*)u8"", NULL);
    }
    Row->Icon = UIElementIcon_IsValid(&content->Icon) ? content->Icon : UIElementIcon_None();
    Row->CellCount = (content->CellCount < UI_LIST_MAX_CELLS) ? content->CellCount : UI_LIST_MAX_CELLS;
    for (size_t Index = 0U; Index < UI_LIST_MAX_CELLS; Index++)
    {
        bool IsUsed = Index < Row->CellCount;
        Row->CellIcons[Index] = (IsUsed && (content->CellIcons != NULL) && UIElementIcon_IsValid(&content->CellIcons[Index]))
            ? content->CellIcons[Index] : UIElementIcon_None();
        Row->Cells[Index] = IsUsed && (content->Cells != NULL) && content->Cells[Index];
    }
    Row->IsDimmed = content->IsDimmed;
    Row->Depth = content->Depth;
    Row->HasChildren = content->HasChildren;
    Row->IsExpanded = content->IsExpanded;
    Row->IsBound = true;
    return Result;
}

void UIListRow_Unbind(UIScreen* screen, uint64_t rowId)
{
    ListRow* Row = ResolveRow(screen, rowId);
    if (Row != NULL)
    {
        Row->IsBound = false;
    }
}

void UIListRow_SetView(UIScreen* screen, uint64_t rowId, const UIListRowView* view, const UIListRowGeometry* geometry)
{
    ListRow* Row = ResolveRow(screen, rowId);
    if (Row != NULL)
    {
        Row->View = *view;
        Row->Geometry = *geometry;
    }
}

bool UIListRow_Peek(UIScreen* screen, uint64_t rowId, const unsigned char** outText, bool* outIsBound, UIListRowView* outView)
{
    UIKit* Kit = UIKit_FromScreen(screen);
    Widget* Found = NULL;
    if ((Kit == NULL) || !UIScreen_TryGetWidgetById(screen, rowId, &Found) || (Widget_GetTypeId(Found) != Kit->_listRowType))
    {
        return false;
    }
    const ListRow* Row = (const ListRow*)(const void*)Found;
    if (outText != NULL)
    {
        *outText = UITextContent_Get(&Row->Content);
    }
    if (outIsBound != NULL)
    {
        *outIsBound = Row->IsBound;
    }
    if (outView != NULL)
    {
        *outView = Row->View;
    }
    return true;
}

float UIListRow_GetContentStart(const UIListRowGeometry* geometry, uint32_t depth)
{
    return geometry->RowPadding + ((float)depth * geometry->Indent);
}

Rectangle UIListRow_GetExpanderBox(const UIListRowGeometry* geometry, uint32_t depth, float rowHeight)
{
    float Width = geometry->HasExpanderColumn ? geometry->Indent : 0.0f;
    return (Rectangle){ .x = UIListRow_GetContentStart(geometry, depth), .y = 0.0f, .width = Width, .height = rowHeight };
}

Rectangle UIListRow_GetCellBox(const UIListRowGeometry* geometry, float rowWidth, float rowHeight, size_t cellCount, size_t cell)
{
    float Side = Math_MaxFloat(0.0f, Math_MinFloat(geometry->CellSize, rowHeight));
    // The last cell stands at the padding from the right edge, each before it a gap of the padding further left.
    float FromRight = geometry->RowPadding + ((float)(cellCount - 1U - cell) * (Side + geometry->RowPadding)) + Side;
    return (Rectangle){ .x = rowWidth - FromRight, .y = (rowHeight - Side) * 0.5f, .width = Side, .height = Side };
}

int32_t UIListRow_FindCell(const UIListRowGeometry* geometry, float rowWidth, float rowHeight, size_t cellCount, Vector2 point)
{
    for (size_t Index = 0U; Index < cellCount; Index++)
    {
        Rectangle Box = UIListRow_GetCellBox(geometry, rowWidth, rowHeight, cellCount, Index);
        if ((Box.width > 0.0f) && (point.x >= Box.x) && (point.x < (Box.x + Box.width)) && (point.y >= Box.y) && (point.y < (Box.y + Box.height)))
        {
            return (int32_t)Index;
        }
    }
    return -1;
}
