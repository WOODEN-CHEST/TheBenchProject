#include "UITree.h"
#include "Logger.h"
#include "UIContextMenu.h"
#include "UIDragDrop.h"
#include "UIKit.h"
#include "UIKitInternal.h"
#include "UIListRow.h"
#include "UIScreen.h"
#include "UIScrollbar.h"
#include "wr/WRComparator.h"
#include "wr/WRMath.h"


// Macros.
/** The narrowest a tree's text column measures, in UI pixels, and how many levels of indent it measures room for. */
#define TREE_MIN_TEXT_WIDTH 96.0f
#define TREE_MEASURED_LEVELS 3.0f
/** How far, in UI pixels, the rows may pass the view and still fit: float noise shows no bar. */
#define FIT_TOLERANCE 0.01f
/** The share of a row at its top and its bottom where a drop goes above or below it rather than into it. */
#define DROP_EDGE_SHARE 0.25f


// Types.
/* One level of the walk: its node (or the top level), how many children and the next to visit. */
typedef struct WalkLevelStruct
{
    bool IsRoot;
    uint64_t Node;
    size_t Count;
    size_t Next;
} WalkLevel;


// Static functions: the selection's ids, rising.
static uint64_t GetIdAt(const GenericBuffer* ids, size_t index)
{
    return *(const uint64_t*)GenericBuffer_GetPointerToElement((GenericBuffer*)ids, index);
}

/* Whether a sorted list holds an id; @p outIndex receives where it is, or where it would go. */
static bool FindId(const GenericBuffer* ids, uint64_t id, size_t* outIndex)
{
    size_t Low = 0U;
    size_t High = ids->_count;
    while (Low < High)
    {
        size_t Middle = Low + ((High - Low) / 2U);
        if (GetIdAt(ids, Middle) < id)
        {
            Low = Middle + 1U;
        }
        else
        {
            High = Middle;
        }
    }
    *outIndex = Low;
    return (Low < ids->_count) && (GetIdAt(ids, Low) == id);
}

static bool ContainsId(const GenericBuffer* ids, uint64_t id)
{
    size_t Unused = 0U;
    return FindId(ids, id, &Unused);
}

/* Adds an id; false when the list could not grow. */
static bool AddId(GenericBuffer* ids, uint64_t id)
{
    size_t At = 0U;
    if (FindId(ids, id, &At))
    {
        return true;
    }
    return GenericBuffer_Insert(ids, &id, At);
}

static void RemoveId(GenericBuffer* ids, uint64_t id)
{
    size_t At = 0U;
    if (FindId(ids, id, &At))
    {
        GenericBuffer_RemoveAt(ids, At);
    }
}

static ComparisonResult CompareIds(GenericBuffer* buffer, GenericBufferElementData a, GenericBufferElementData b, const UserData* userData)
{
    (void)buffer;
    (void)userData;
    uint64_t A = *(const uint64_t*)a._element;
    uint64_t B = *(const uint64_t*)b._element;
    return (A < B) ? ComparisonResult_ALessThanB : ((A > B) ? ComparisonResult_AGreaterThanB : ComparisonResult_AEqualsB);
}

/* Drops the repeats of a sorted list of ids. */
static void DropRepeats(GenericBuffer* ids)
{
    size_t Written = 0U;
    for (size_t Index = 0U; Index < ids->_count; Index++)
    {
        uint64_t Id = GetIdAt(ids, Index);
        if ((Written == 0U) || (GetIdAt(ids, Written - 1U) != Id))
        {
            *(uint64_t*)GenericBuffer_GetPointerToElement(ids, Written) = Id;
            Written++;
        }
    }
    GenericBuffer_SetCount(ids, Written);
}

/* Sorts a list of ids and drops its repeats. */
static void SortUnique(GenericBuffer* ids)
{
    unsigned char Scratch[2U * sizeof(uint64_t)];
    GenericBuffer_SortAscending(ids, CompareIds, NULL, Scratch);
    DropRepeats(ids);
}

/* Merges sorted, unique @p add into sorted, unique @p into, from the back so it needs no other room; false when it could not grow. */
static bool MergeIds(GenericBuffer* into, const GenericBuffer* add)
{
    size_t Old = into->_count;
    if (!GenericBuffer_EnsureTotalCapacity(into, Old + add->_count) || !GenericBuffer_SetCount(into, Old + add->_count))
    {
        return false;
    }
    size_t Left = Old;
    size_t Right = add->_count;
    size_t Write = Old + add->_count;
    while (Right > 0U)
    {
        uint64_t FromAdd = GetIdAt(add, Right - 1U);
        if ((Left > 0U) && (GetIdAt(into, Left - 1U) > FromAdd))
        {
            *(uint64_t*)GenericBuffer_GetPointerToElement(into, --Write) = GetIdAt(into, --Left);
        }
        else
        {
            *(uint64_t*)GenericBuffer_GetPointerToElement(into, --Write) = FromAdd;
            Right--;
        }
    }
    DropRepeats(into);
    return true;
}


// Static functions: helpers.
static UITreeCapability* GetCapability(void* widget)
{
    return &((UITreeLookBase*)widget)->Tree;
}

static Error ResolveTree(UIScreen* screen, uint64_t widgetId, Widget** outWidget, UITreeCapability** outTree)
{
    UIKit* Kit = (screen != NULL) ? UIKit_FromScreen(screen) : NULL;
    Widget* Target = NULL;
    if ((Kit == NULL) || !UIScreen_TryGetWidgetById(screen, widgetId, &Target))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UITree: the widget is gone, or its screen has no kit.");
    }
    UITreeCapability* Tree = Widget_GetCapability(Target, UIKit_GetCapabilityId(Kit, UIElementCapability_Tree));
    if (Tree == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UITree: the widget is not a tree.");
    }
    *outWidget = Target;
    *outTree = Tree;
    return Error_CreateSuccess();
}

static float CleanLength(float value)
{
    return (Math_IsNaNFloat(value) || Math_IsInfinityFloat(value) || !(value > 0.0f)) ? 0.0f : value;
}

static const UITreeRow* GetRowAt(const UITreeCapability* self, size_t row)
{
    return GenericBuffer_GetPointerToElement((GenericBuffer*)&self->_rows, row);
}

static size_t GetRowCount(const UITreeCapability* self)
{
    return self->_rows._count;
}

static UIListRowGeometry GetRowGeometry(const UITreeCapability* self)
{
    return (UIListRowGeometry){ .RowPadding = self->_metrics.RowPadding, .CellSize = self->_metrics.CellSize,
        .InsertionThickness = self->_metrics.InsertionThickness, .Indent = self->_metrics.Indent, .HasExpanderColumn = true };
}

static Vector2 GetBoxSize(const Widget* widget)
{
    Rectangle Pixels = UIScreen_GetWidgetPixelBox(Widget_GetScreen(widget), widget);
    float Scale = Widget_GetEffectiveScale(widget);
    return (Vector2){ .x = Pixels.width / Scale, .y = Pixels.height / Scale };
}

static Vector2 ToContent(const UITreeCapability* self, const Widget* widget, Vector2 widgetPosition)
{
    Vector2 Box = GetBoxSize(widget);
    return (Vector2){ .x = (widgetPosition.x * Box.x) - self->_metrics.Padding.x,
        .y = (widgetPosition.y * Box.y) - self->_metrics.Padding.y + self->Scrollable._offset.y };
}

static bool TryGetRowAt(const UITreeCapability* self, const Widget* widget, Vector2 widgetPosition, size_t* outRow, Vector2* outInRow)
{
    Vector2 Content = ToContent(self, widget, widgetPosition);
    float ViewTop = self->Scrollable._offset.y;
    bool IsInside = (Content.x >= 0.0f) && (Content.x < self->Scrollable._viewSize.x) && (Content.y >= ViewTop)
        && (Content.y < (ViewTop + self->Scrollable._viewSize.y));
    if (!IsInside)
    {
        return false;
    }
    double Row = Math_FloorDouble((double)Content.y / (double)self->_metrics.RowHeight);
    if ((Row < 0.0) || !(Row < (double)GetRowCount(self)))
    {
        return false;
    }
    *outRow = (size_t)Row;
    if (outInRow != NULL)
    {
        *outInRow = (Vector2){ .x = Content.x, .y = (float)((double)Content.y - (Row * (double)self->_metrics.RowHeight)) };
    }
    return true;
}

/* Whether a point of a row (UI pixels from its top-left) lies on its expander: a node with children only. */
static bool IsOnExpander(const UITreeCapability* self, size_t row, Vector2 inRow)
{
    const UITreeRow* Row = GetRowAt(self, row);
    if (!Row->HasChildren)
    {
        return false;
    }
    UIListRowGeometry Geometry = GetRowGeometry(self);
    Rectangle Box = UIListRow_GetExpanderBox(&Geometry, Row->Depth, self->_metrics.RowHeight);
    return (inRow.x >= Box.x) && (inRow.x < (Box.x + Box.width));
}

/* The row a node shows at: the hint first, then a search. */
static bool FindRowOfNode(UITreeCapability* self, uint64_t node, size_t* outRow)
{
    size_t Count = GetRowCount(self);
    if ((self->_currentRowHint < Count) && (GetRowAt(self, self->_currentRowHint)->Node == node))
    {
        *outRow = self->_currentRowHint;
        return true;
    }
    for (size_t Row = 0U; Row < Count; Row++)
    {
        if (GetRowAt(self, Row)->Node == node)
        {
            *outRow = Row;
            return true;
        }
    }
    return false;
}

static UIEventMouse MakeMouse(const WidgetMouseInputArgs* args)
{
    bool IsOver = (args->WidgetPosition.x >= 0.0f) && (args->WidgetPosition.x <= 1.0f) && (args->WidgetPosition.y >= 0.0f)
        && (args->WidgetPosition.y <= 1.0f);
    return (UIEventMouse){ .Button = args->Button, .Modifiers = args->Modifiers, .ScreenPosition = args->ScreenPosition,
        .ScrollDelta = { .x = 0.0f, .y = 0.0f }, .IsOverWidget = IsOver || (args->Type == UIMouseInputType_ReleaseOver) };
}

static Error PostNodeEvent(Widget* widget, UIElementEvent event, uint64_t node)
{
    UIPropertyValue Value = UIPropertyValue_FromUInt64(node);
    return UIScreen_PostWidgetEvent(Widget_GetScreen(widget), Widget_GetId(widget), (int32_t)event, &Value);
}

static Error PostSelectionChange(Widget* widget, const UITreeCapability* self)
{
    UIScreen* Screen = Widget_GetScreen(widget);
    UIPropertyValue Value = UIPropertyValue_FromDouble((double)self->_selection._count);
    Error Result = UIScreen_PostWidgetEvent(Screen, Widget_GetId(widget), UIElementEvent_Changed, &Value);
    return (Result.Code == ErrorCode_Success) ? UIScreen_PostWidgetEvent(Screen, Widget_GetId(widget), UIElementEvent_Committed, &Value) : Result;
}

static Error SelectionGrowError(void)
{
    return Error_Construct2(ErrorCode_BufferTooLarge, "UITree: the selection could not grow.");
}

static bool ClearAll(UITreeCapability* self)
{
    bool HadAny = self->_selection._count > 0U;
    GenericBuffer_Clear(&self->_selection);
    return HadAny;
}

static bool SelectOnly(UITreeCapability* self, uint64_t node, Error* outResult)
{
    if ((self->_selection._count == 1U) && (GetIdAt(&self->_selection, 0U) == node))
    {
        return false;
    }
    ClearAll(self);
    if (!AddId(&self->_selection, node))
    {
        *outResult = SelectionGrowError();
    }
    return true;
}

/* Selects the nodes of the rows from @p fromRow to @p toRow, both in, in either order; @p isAdding keeps what was selected. Returns
 * whether the selection changed. */
static bool SelectRows(UITreeCapability* self, size_t fromRow, size_t toRow, bool isAdding, Error* outResult)
{
    size_t First = Math_MinSizeT(fromRow, toRow);
    size_t Last = Math_MaxSizeT(fromRow, toRow);
    GenericBuffer_Clear(&self->_gathered);
    if (!GenericBuffer_EnsureTotalCapacity(&self->_gathered, Last - First + 1U))
    {
        *outResult = SelectionGrowError();
        return false;
    }
    for (size_t Row = First; Row <= Last; Row++)
    {
        uint64_t Node = GetRowAt(self, Row)->Node;
        GenericBuffer_AddLast(&self->_gathered, &Node); // Cannot fail: the room is there.
    }
    SortUnique(&self->_gathered);
    size_t Before = self->_selection._count;
    if (!isAdding)
    {
        bool IsSame = Before == self->_gathered._count;
        for (size_t Index = 0U; IsSame && (Index < Before); Index++)
        {
            IsSame = GetIdAt(&self->_selection, Index) == GetIdAt(&self->_gathered, Index);
        }
        if (IsSame)
        {
            return false;
        }
        // The gathered ids become the selection: the two buffers trade places.
        GenericBuffer Swap = self->_selection;
        self->_selection = self->_gathered;
        self->_gathered = Swap;
        return true;
    }
    if (!MergeIds(&self->_selection, &self->_gathered))
    {
        *outResult = SelectionGrowError();
        return false;
    }
    return self->_selection._count != Before;
}

static void SetCurrent(UITreeCapability* self, uint64_t node, size_t row)
{
    self->_hasCurrent = true;
    self->_current = node;
    self->_currentRowHint = row;
}

static void RevealRow(UITreeCapability* self, size_t row)
{
    float RowHeight = self->_metrics.RowHeight;
    Rectangle Box = { .x = 0.0f, .y = (float)((double)row * (double)RowHeight), .width = 1.0f, .height = RowHeight };
    UIScrollableCapability_EnsureVisible(&self->Scrollable, Box);
}

static void ForgetPress(UITreeCapability* self)
{
    self->_hasPress = false;
    self->_hasPressNode = false;
    self->_pressCell = -1;
    self->_isSelectOnRelease = false;
    self->_hasContextNode = false;
}


// Static functions: the walk.
/* Walks the expanded part of the data into rows: each node, then its children a level deeper when it is expanded. */
static Error WalkRows(Widget* widget, UITreeCapability* self)
{
    UIScreen* Screen = Widget_GetScreen(widget);
    uint64_t TreeId = Widget_GetId(widget);
    const UITreeProvider* Provider = self->_provider;
    const UserData* Data = &self->_providerData;
    GenericBuffer_Clear(&self->_rows);
    self->_isDirty = false;
    UIRowRecycler_Invalidate(&self->_recycler);
    if (Provider == NULL)
    {
        return Error_CreateSuccess();
    }
    WalkLevel Stack[UI_TREE_MAX_DEPTH + 1U];
    Stack[0] = (WalkLevel){ .IsRoot = true, .Node = 0U, .Count = Provider->GetChildCount(Screen, TreeId, Data, true, 0U), .Next = 0U };
    size_t Levels = 1U;
    bool IsCut = false;
    while (Levels > 0U)
    {
        WalkLevel* Level = &Stack[Levels - 1U];
        if (Level->Next >= Level->Count)
        {
            Levels--;
            continue;
        }
        if (GetRowCount(self) >= UI_TREE_MAX_ROWS)
        {
            IsCut = true;
            break;
        }
        uint64_t Child = Provider->GetChild(Screen, TreeId, Data, Level->IsRoot, Level->Node, Level->Next);
        Level->Next++;
        size_t ChildCount = Provider->GetChildCount(Screen, TreeId, Data, false, Child);
        bool IsExpanded = (ChildCount > 0U) && Provider->IsExpanded(Screen, TreeId, Data, Child);
        UITreeRow Row = { .Node = Child, .Depth = (uint32_t)(Levels - 1U), .HasChildren = ChildCount > 0U, .IsExpanded = IsExpanded };
        if (!GenericBuffer_AddLast(&self->_rows, &Row))
        {
            return Error_Construct2(ErrorCode_BufferTooLarge, "UITree: the rows could not grow.");
        }
        if (IsExpanded && (Levels < UI_TREE_MAX_DEPTH))
        {
            Stack[Levels] = (WalkLevel){ .IsRoot = false, .Node = Child, .Count = ChildCount, .Next = 0U };
            Levels++;
        }
    }
    if (IsCut && !self->_isRowLimitReported)
    {
        self->_isRowLimitReported = true;
        Logger* Log = UIScreen_GetLogger(Screen);
        Error LogResult = (Log != NULL) ? Logger_LogWarning(Log, (const unsigned char*)u8"UITree: a tree shows UI_TREE_MAX_ROWS rows at most; the rest of its data is left out.")
            : Error_CreateSuccess();
        Error_Deconstruct(&LogResult);
    }
    // The selected nodes that are gone drop out, at a refresh.
    if (self->_isSelectionCheckPending)
    {
        self->_isSelectionCheckPending = false;
        for (size_t Index = self->_selection._count; Index-- > 0U;)
        {
            if (!Provider->HasNode(Screen, TreeId, Data, GetIdAt(&self->_selection, Index)))
            {
                GenericBuffer_RemoveAt(&self->_selection, Index);
            }
        }
    }
    // The current node keeps its row; one no longer shown gives its place to the row that took it.
    size_t Row = 0U;
    if (self->_hasCurrent && !FindRowOfNode(self, self->_current, &Row))
    {
        size_t Count = GetRowCount(self);
        self->_hasCurrent = Count > 0U;
        Row = (self->_currentRowHint < Count) ? self->_currentRowHint : ((Count > 0U) ? (Count - 1U) : 0U);
        self->_current = (Count > 0U) ? GetRowAt(self, Row)->Node : 0U;
    }
    self->_currentRowHint = Row;
    return Error_CreateSuccess();
}

/* Expands or collapses a row's node: a collapse that hides the current row makes the node current. The rows are walked again at once. */
static Error ToggleRow(Widget* widget, UITreeCapability* self, size_t row)
{
    UITreeRow Row = *GetRowAt(self, row);
    if (!Row.HasChildren || (self->_provider == NULL))
    {
        return Error_CreateSuccess();
    }
    size_t CurrentRow = 0U;
    if (Row.IsExpanded && self->_hasCurrent && FindRowOfNode(self, self->_current, &CurrentRow) && (CurrentRow > row))
    {
        // Inside its subtree: the rows after it deeper than it.
        bool IsInside = true;
        for (size_t Between = row + 1U; IsInside && (Between <= CurrentRow); Between++)
        {
            IsInside = GetRowAt(self, Between)->Depth > Row.Depth;
        }
        if (IsInside)
        {
            SetCurrent(self, Row.Node, row);
        }
    }
    self->_provider->SetExpanded(Widget_GetScreen(widget), Widget_GetId(widget), &self->_providerData, Row.Node, !Row.IsExpanded);
    Error Result = WalkRows(widget, self);
    return (Result.Code == ErrorCode_Success) ? UIElement_PostCue(widget, Row.IsExpanded ? UIElementCue_Close : UIElementCue_Open) : Result;
}


// Static functions: the rows.
static Error Rows_Create(void* owner, uint64_t* outWidgetId)
{
    Widget* Base = owner;
    UITreeCapability* Self = GetCapability(owner);
    UIScreen* Screen = Widget_GetScreen(Base);
    UIKit* Kit = UIKit_FromScreen(Screen);
    const UIElementLook* Look = Widget_GetCapability(Base, UIKit_GetCapabilityId(Kit, UIElementCapability_Look));
    const unsigned char* Variant = ((Look != NULL) && (Look->_variant[0] != 0U)) ? Look->_variant : NULL;
    return UIListRow_Create(Screen, Kit, Self->_rowsBoxId, UIListRow_GetTreeParts(), Variant, outWidgetId);
}

static Error Rows_Bind(void* owner, uint64_t widgetId, size_t dataIndex)
{
    Widget* Base = owner;
    UITreeCapability* Self = GetCapability(owner);
    UIScreen* Screen = Widget_GetScreen(Base);
    UITreeRow Row = *GetRowAt(Self, dataIndex);
    UIListRowData Data = { .Text = &Self->_scratch, .Icon = UIElementIcon_None(), .IsDimmed = false };
    for (size_t Index = 0U; Index < UI_LIST_MAX_CELLS; Index++)
    {
        Data.Cells[Index] = false;
    }
    GenericBuffer_Clear(&Self->_scratch);
    if (Self->_provider != NULL)
    {
        Error Result = Self->_provider->WriteNode(Screen, Widget_GetId(Base), &Self->_providerData, Row.Node, &Data);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    size_t Length = 0U;
    while ((Length < Self->_scratch._count) && (Self->_scratch._data[Length] != 0U) && (Length < UI_LIST_MAX_TEXT_BYTES))
    {
        Length++;
    }
    while ((Length > 0U) && (Length < Self->_scratch._count) && ((Self->_scratch._data[Length] & 0xC0U) == 0x80U))
    {
        Length--;
    }
    if (!GenericBuffer_SetCount(&Self->_scratch, Length) || !GenericBuffer_NullTerminate(&Self->_scratch))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "UITree: a node's text could not be held.");
    }
    UIListRowContent Content = { .Text = Self->_scratch._data, .Icon = Data.Icon, .IsDimmed = Data.IsDimmed, .CellCount = Self->_cellCount,
        .CellIcons = Self->_cellIcons, .Cells = Data.Cells, .Depth = Row.Depth, .HasChildren = Row.HasChildren, .IsExpanded = Row.IsExpanded };
    return UIListRow_Bind(Screen, widgetId, &Content);
}

static Error Rows_Unbind(void* owner, uint64_t widgetId)
{
    UIListRow_Unbind(Widget_GetScreen((Widget*)owner), widgetId);
    return Error_CreateSuccess();
}

static const UIRowRecyclerHooks RowHooks = { .CreateRow = Rows_Create, .BindRow = Rows_Bind, .UnbindRow = Rows_Unbind };

static void PushRowViews(UITreeCapability* self, Widget* widget)
{
    UIScreen* Screen = Widget_GetScreen(widget);
    UIListRowGeometry Geometry = GetRowGeometry(self);
    bool IsMarked = Widget_IsFocused(widget) && self->_hasCurrent;
    bool IsAccepting = UIDragDrop_GetTargetState(Screen, Widget_GetId(widget)) == UIDragTargetState_Accepting;
    for (size_t Index = 0U; Index < UIRowRecycler_GetCount(&self->_recycler); Index++)
    {
        const UIRecycledRow* Recycled = UIRowRecycler_GetAt(&self->_recycler, Index);
        UIListRowView View = { .IsEnabled = self->_isEnabled, .IsSelected = false, .IsHovered = false, .IsCurrentShown = false,
            .IsOwnerEntered = Widget_IsEntered(widget), .HoveredCell = -1, .PressedCell = -1, .IsExpanderHovered = false,
            .Insertion = UIListRowInsertion_None };
        if (Recycled->IsBound && (Recycled->DataIndex < GetRowCount(self)))
        {
            size_t Row = Recycled->DataIndex;
            uint64_t Node = GetRowAt(self, Row)->Node;
            View.IsSelected = ContainsId(&self->_selection, Node);
            View.IsHovered = self->_hasHovered && (self->_hoveredRow == Row);
            View.IsCurrentShown = IsMarked && (self->_current == Node);
            View.HoveredCell = View.IsHovered ? self->_hoveredCell : -1;
            View.IsExpanderHovered = View.IsHovered && self->_isExpanderHovered;
            View.PressedCell = (self->_hasPressNode && (self->_pressNode == Node)) ? self->_pressCell : -1;
            if (IsAccepting && self->_hasInsertion && (self->_insertionRow == Row))
            {
                View.Insertion = (self->_insertionEdge == UITreeDropEdge_Above) ? UIListRowInsertion_Above
                    : ((self->_insertionEdge == UITreeDropEdge_Below) ? UIListRowInsertion_Below : UIListRowInsertion_Into);
            }
        }
        UIListRow_SetView(Screen, Recycled->WidgetId, &View, &Geometry);
    }
}

static void TrackHover(UITreeCapability* self, Widget* widget)
{
    UIScreen* Screen = Widget_GetScreen(widget);
    self->_hasHovered = false;
    self->_hoveredCell = -1;
    self->_isExpanderHovered = false;
    if (!Widget_IsHovered(widget) || !UIScreen_IsPointerPresent(Screen) || UIDragDrop_IsDragging(Screen))
    {
        return; // During a drag what shows where it goes is the drag's: the frame and the insertion line.
    }
    Vector2 Local = UIScreen_ScreenToWidget(Screen, widget, UIScreen_GetPointerPosition(Screen));
    size_t Row = 0U;
    Vector2 InRow = { .x = 0.0f, .y = 0.0f };
    if (TryGetRowAt(self, widget, Local, &Row, &InRow))
    {
        UIListRowGeometry Geometry = GetRowGeometry(self);
        self->_hasHovered = true;
        self->_hoveredRow = Row;
        self->_hoveredCell = UIListRow_FindCell(&Geometry, self->Scrollable._viewSize.x, self->_metrics.RowHeight, self->_cellCount, InRow);
        self->_isExpanderHovered = IsOnExpander(self, Row, InRow);
    }
}

/* Where a drop at a point of the content would go: the row under it and the edge by where in the row it falls. */
static UITreeDropEdge GetDropPlace(const UITreeCapability* self, float contentY, size_t* outRow)
{
    size_t Count = GetRowCount(self);
    *outRow = 0U;
    if (Count == 0U)
    {
        return UITreeDropEdge_None;
    }
    double Place = (double)contentY / (double)self->_metrics.RowHeight;
    if (Place < 0.0)
    {
        return UITreeDropEdge_Above;
    }
    if (!(Place < (double)Count))
    {
        *outRow = Count - 1U;
        return UITreeDropEdge_Below;
    }
    *outRow = (size_t)Math_FloorDouble(Place);
    double InRow = Place - Math_FloorDouble(Place);
    return (InRow < (double)DROP_EDGE_SHARE) ? UITreeDropEdge_Above : ((InRow > (1.0 - (double)DROP_EDGE_SHARE)) ? UITreeDropEdge_Below : UITreeDropEdge_Into);
}

static void TrackDrag(UITreeCapability* self, Widget* widget, double passedSeconds)
{
    UIScreen* Screen = Widget_GetScreen(widget);
    UIDragTargetState Target = UIDragDrop_GetTargetState(Screen, Widget_GetId(widget));
    self->_hasInsertion = false;
    UIDragSession Session;
    if ((Target == UIDragTargetState_None) || !UIDragDrop_GetSession(Screen, &Session))
    {
        self->_dragScrollSeconds = 0.0;
        return;
    }
    Vector2 Local = UIScreen_ScreenToWidget(Screen, widget, Session.ScreenPosition);
    Vector2 Content = ToContent(self, widget, Local);
    float InView = Content.y - self->Scrollable._offset.y;
    float Edge = self->_metrics.RowHeight * UI_LIST_DRAG_SCROLL_ROWS;
    float Direction = (InView < Edge) ? -1.0f : ((InView > (self->Scrollable._viewSize.y - Edge)) ? 1.0f : 0.0f);
    if ((Direction != 0.0f) && UIScrollableCapability_CanScroll(&self->Scrollable, UIScrollAxis_Vertical, Direction))
    {
        self->_dragScrollSeconds += passedSeconds;
        double Rows = Math_FloorDouble(self->_dragScrollSeconds * (double)UI_LIST_DRAG_SCROLL_ROWS_PER_SECOND);
        if (Rows >= 1.0)
        {
            self->_dragScrollSeconds -= Rows / (double)UI_LIST_DRAG_SCROLL_ROWS_PER_SECOND;
            UIScrollableCapability_ScrollBy(&self->Scrollable, (Vector2){ .x = 0.0f, .y = Direction * (float)Rows * self->_metrics.RowHeight });
            Content = ToContent(self, widget, Local);
        }
    }
    else
    {
        self->_dragScrollSeconds = 0.0;
    }
    if (Target == UIDragTargetState_Accepting)
    {
        self->_insertionEdge = GetDropPlace(self, Content.y, &self->_insertionRow);
        self->_hasInsertion = self->_insertionEdge != UITreeDropEdge_None;
    }
}


// Static functions: the behaviour.
static void* ResolveTreeCapability(void* widget)
{
    return GetCapability(widget);
}

static void* ResolveScrollable(void* widget)
{
    return &GetCapability(widget)->Scrollable;
}

static bool IsProviderWhole(const UITreeProvider* provider)
{
    return (provider->GetChildCount != NULL) && (provider->GetChild != NULL) && (provider->IsExpanded != NULL) && (provider->SetExpanded != NULL)
        && (provider->WriteNode != NULL) && (provider->HasNode != NULL);
}

static bool AreOptionsValid(const UITreeOptions* options)
{
    bool IsValid = UISelectionMode_IsValid(options->SelectionMode) && (options->CellCount <= UI_LIST_MAX_CELLS)
        && (options->VisibleRows >= 1U) && (options->VisibleRows <= UI_LIST_MAX_VISIBLE_ROWS)
        && ((options->Provider == NULL) || IsProviderWhole(options->Provider));
    for (size_t Index = 0U; IsValid && (Index < options->CellCount); Index++)
    {
        IsValid = UIElementIcon_IsValid(&options->CellIcons[Index]);
    }
    return IsValid;
}

static Error DragProvider(UIScreen* screen, uint64_t sourceId, Vector2 pressScreenPosition, const UserData* userData, UIDragPayload* outPayload,
    bool* outIsDragged);

static Error Behaviour_Construct(void* widget, void* args)
{
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UITree: the arguments are required.");
    }
    const UITreeOptions* Options = ((const UITreeArgs*)args)->Options;
    if (!AreOptionsValid(Options))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "UITree: a known selection mode, at most UI_LIST_MAX_CELLS valid cells, a visible row count within [1, UI_LIST_MAX_VISIBLE_ROWS] and a provider with every hook (or none) are required.");
    }
    Error Result = UIElement_ApplyOptions(widget, &Options->Element);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    UITreeCapability* Self = GetCapability(widget);
    UIScrollableCapability_Construct(&Self->Scrollable, UIScrollMode_Disabled, UIScrollMode_Auto, 22.0f * UI_LIST_WHEEL_ROWS);
    Self->_provider = Options->Provider;
    Self->_providerData = Options->ProviderData;
    GenericBuffer_AllocateVariable(&Self->_rows, 16U, sizeof(UITreeRow));
    Self->_isDirty = true;
    Self->_isSelectionCheckPending = false;
    Self->_isRowLimitReported = false;
    Self->_mode = Options->SelectionMode;
    GenericBuffer_AllocateVariable(&Self->_selection, 8U, sizeof(uint64_t));
    GenericBuffer_AllocateVariable(&Self->_gathered, 8U, sizeof(uint64_t));
    Self->_hasCurrent = false;
    Self->_current = 0U;
    Self->_currentRowHint = 0U;
    Self->_hasAnchor = false;
    Self->_anchor = 0U;
    Self->_hasHovered = false;
    Self->_hoveredRow = 0U;
    Self->_hoveredCell = -1;
    Self->_isExpanderHovered = false;
    UIRowRecycler_Construct(&Self->_recycler);
    Self->_rowsBoxId = WIDGET_ID_INVALID;
    Self->_barId = WIDGET_ID_INVALID;
    Self->_metrics = (UITreeMetrics){ .Padding = { .x = 1.0f, .y = 1.0f }, .RowHeight = 22.0f, .RowPadding = 6.0f, .Indent = 16.0f, .CellSize = 14.0f,
        .InsertionThickness = 2.0f };
    Self->_barThickness = 0.0f;
    Self->_isBarShown = false;
    Self->_cellCount = Options->CellCount;
    for (size_t Index = 0U; Index < UI_LIST_MAX_CELLS; Index++)
    {
        Self->_cellIcons[Index] = (Index < Options->CellCount) ? Options->CellIcons[Index] : UIElementIcon_None();
    }
    Self->_visibleRows = Options->VisibleRows;
    Self->_isEnabled = Options->IsEnabled;
    Self->_isDragSource = Options->IsDragSource;
    Self->_dragKind = Options->DragKind;
    Self->_isRevealPending = false;
    GenericBuffer_AllocateVariable(&Self->_scratch, 64U, sizeof(unsigned char));
    Self->_pressButton = GameKey_Unknown;
    Self->_pressNode = 0U;
    Self->_contextNode = 0U;
    ForgetPress(Self);
    Self->_dragScrollSeconds = 0.0;
    Self->_hasInsertion = false;
    Self->_insertionRow = 0U;
    Self->_insertionEdge = UITreeDropEdge_None;
    if (!Options->IsEnabled)
    {
        Error Focus = Widget_SetIsFocusable(widget, false); // The widget is not alive yet: nothing can fail or run.
        Error_Deconstruct(&Focus);
    }
    return Error_CreateSuccess();
}

static Error Behaviour_Deconstruct(void* widget)
{
    UITreeCapability* Self = GetCapability(widget);
    Memory_Free(Self->_rows._data);
    Memory_Free(Self->_selection._data);
    Memory_Free(Self->_gathered._data);
    Memory_Free(Self->_scratch._data);
    UIRowRecycler_Deconstruct(&Self->_recycler);
    return Error_CreateSuccess();
}

static Error Behaviour_Update(void* widget, ProgramTime time)
{
    UITreeCapability* Self = GetCapability(widget);
    UIScreen* Screen = Widget_GetScreen(widget);
    uint64_t TreeId = Widget_GetId(widget);
    if (!UIScreen_IsWidgetAlive(Screen, Self->_rowsBoxId))
    {
        return Error_CreateSuccess(); // Its parts come once it is attached (UIKit_CreateTree).
    }
    Error Result = Self->_isDirty ? WalkRows(widget, Self) : Error_CreateSuccess();
    Widget* Found = NULL;
    if ((Result.Code != ErrorCode_Success) || !UIScreen_TryGetWidgetById(Screen, TreeId, &Found))
    {
        return Result;
    }
    Self = GetCapability(Found);
    TrackHover(Self, Found);
    TrackDrag(Self, Found, time.PassedTime);
    Result = UIRowRecycler_Sync(&Self->_recycler, Screen, GetRowCount(Self), Self->_metrics.RowHeight, Self->Scrollable._offset.y,
        Self->Scrollable._viewSize.y, &RowHooks, Found);
    if ((Result.Code != ErrorCode_Success) || !UIScreen_TryGetWidgetById(Screen, TreeId, &Found))
    {
        return Result;
    }
    PushRowViews(GetCapability(Found), Found);
    return Error_CreateSuccess();
}

static Error Behaviour_Layout(void* widget, const UILayoutContext* context)
{
    UITreeCapability* Self = GetCapability(widget);
    UIScreen* Screen = Widget_GetScreen(widget);
    Vector2 Box = GetBoxSize(widget);
    float Thickness = 0.0f;
    Error Result = UIScrollbar_MeasureThickness(Screen, Self->_barId, UIScrollAxis_Vertical, context->Sizing, &Thickness);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Vector2 Padding = Self->_metrics.Padding;
    float RowHeight = Self->_metrics.RowHeight;
    double Content = (double)GetRowCount(Self) * (double)RowHeight;
    float InnerWidth = Math_MaxFloat(0.0f, Box.x - (2.0f * Padding.x));
    float InnerHeight = Math_MaxFloat(0.0f, Box.y - (2.0f * Padding.y));
    UIScrollMode Mode = Self->Scrollable._modes[UIScrollAxis_Vertical];
    bool IsBarShown = (Mode == UIScrollMode_Always) || ((Mode == UIScrollMode_Auto) && (Content > (double)(InnerHeight + FIT_TOLERANCE)));
    float Bar = IsBarShown ? Thickness : 0.0f;
    Rectangle Rows = { .x = Padding.x, .y = Padding.y, .width = Math_MaxFloat(0.0f, InnerWidth - Bar), .height = InnerHeight };
    float OffsetBefore = Self->Scrollable._offset.y;
    UIScrollableCapability_SetSizes(&Self->Scrollable, (Vector2){ .x = Rows.width, .y = (float)Content }, (Vector2){ .x = Rows.width, .y = Rows.height });
    Self->Scrollable._wheelStep = RowHeight * UI_LIST_WHEEL_ROWS;
    size_t CurrentRow = 0U;
    if (Self->_isRevealPending && (Rows.height > 0.0f) && !Self->_isDirty)
    {
        if (Self->_hasCurrent && FindRowOfNode(Self, Self->_current, &CurrentRow))
        {
            RevealRow(Self, CurrentRow);
        }
        Self->_isRevealPending = false;
    }
    Self->_isBarShown = IsBarShown;
    Self->_barThickness = Thickness;
    Widget* RowsBox = NULL;
    if (UIScreen_TryGetWidgetById(Screen, Self->_rowsBoxId, &RowsBox))
    {
        UIPlacement Placement = UIPlacement_FromTopLeft(Rows.x, Rows.y, Rows.width, Rows.height);
        Result = Widget_SetPlacement(RowsBox, &Placement);
    }
    if (Result.Code == ErrorCode_Success)
    {
        UIInsets Edge = { .Left = Padding.x, .Top = Padding.y, .Right = Padding.x, .Bottom = Padding.y };
        Result = UIScrollbar_PlaceAlongEdge(Screen, Self->_barId, UIScrollAxis_Vertical, IsBarShown, Thickness, 0.0f, Edge,
            Widget_GetEffectiveScale(widget));
    }
    if ((Result.Code == ErrorCode_Success) && (Self->Scrollable._offset.y != OffsetBefore))
    {
        uint64_t TreeId = Widget_GetId(widget);
        Result = UIRowRecycler_Sync(&Self->_recycler, Screen, GetRowCount(Self), RowHeight, Self->Scrollable._offset.y,
            Self->Scrollable._viewSize.y, &RowHooks, widget);
        Widget* Found = NULL;
        if ((Result.Code != ErrorCode_Success) || !UIScreen_TryGetWidgetById(Screen, TreeId, &Found))
        {
            return Result;
        }
        Self = GetCapability(Found);
        PushRowViews(Self, Found);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIRowRecycler_Place(&Self->_recycler, Screen, RowHeight, Self->Scrollable._offset.y);
    }
    Self->Scrollable._placedOffset = Self->Scrollable._offset;
    Self->Scrollable._viewWidgetId = Self->_rowsBoxId;
    return Result;
}

static Error Behaviour_OnHoverStart(void* widget, const WidgetHoverArgs* args)
{
    (void)args;
    return UIElement_PostCue(widget, UIElementCue_Hover);
}

static Error PressRow(Widget* widget, UITreeCapability* self, size_t row, uint8_t modifiers)
{
    uint64_t Node = GetRowAt(self, row)->Node;
    bool IsControl = (modifiers & GAME_KEY_MODIFIER_CONTROL) != 0U;
    bool IsShift = (modifiers & GAME_KEY_MODIFIER_SHIFT) != 0U;
    Error Result = Error_CreateSuccess();
    bool HasChanged = false;
    switch (self->_mode)
    {
        case UISelectionMode_None:
            break;
        case UISelectionMode_Single:
            if (IsControl && ContainsId(&self->_selection, Node))
            {
                HasChanged = ClearAll(self);
            }
            else
            {
                HasChanged = SelectOnly(self, Node, &Result);
            }
            break;
        case UISelectionMode_Multiple:
            if (IsShift)
            {
                size_t From = row;
                if (!(self->_hasAnchor && FindRowOfNode(self, self->_anchor, &From)) && self->_hasCurrent)
                {
                    FindRowOfNode(self, self->_current, &From);
                }
                HasChanged = SelectRows(self, From, row, IsControl, &Result);
            }
            else if (IsControl)
            {
                bool IsSelected = ContainsId(&self->_selection, Node);
                HasChanged = true;
                if (IsSelected)
                {
                    RemoveId(&self->_selection, Node);
                }
                else if (!AddId(&self->_selection, Node))
                {
                    Result = SelectionGrowError();
                }
            }
            else if (ContainsId(&self->_selection, Node) && (self->_selection._count > 1U))
            {
                self->_isSelectOnRelease = true;
            }
            else
            {
                HasChanged = SelectOnly(self, Node, &Result);
            }
            break;
    }
    if (!IsShift)
    {
        self->_hasAnchor = true;
        self->_anchor = Node;
    }
    SetCurrent(self, Node, row);
    if ((Result.Code != ErrorCode_Success) || !HasChanged)
    {
        return Result;
    }
    return PostSelectionChange(widget, self);
}

static Error OnButtonPress(Widget* widget, UITreeCapability* self, const WidgetMouseInputArgs* args)
{
    size_t Row = 0U;
    Vector2 InRow = { .x = 0.0f, .y = 0.0f };
    bool HasRow = TryGetRowAt(self, widget, args->WidgetPosition, &Row, &InRow);
    uint64_t Node = HasRow ? GetRowAt(self, Row)->Node : 0U;
    self->_hasPress = true;
    self->_pressButton = args->Button;
    self->_hasPressNode = HasRow;
    self->_pressNode = Node;
    self->_pressCell = -1;
    self->_isSelectOnRelease = false;
    if (args->Button == GameKey_MouseRight)
    {
        self->_hasContextNode = HasRow;
        self->_contextNode = Node;
        if (!HasRow)
        {
            return Error_CreateSuccess();
        }
        SetCurrent(self, Node, Row);
        if (ContainsId(&self->_selection, Node) || (self->_mode == UISelectionMode_None))
        {
            return Error_CreateSuccess();
        }
        Error Result = Error_CreateSuccess();
        bool HasChanged = SelectOnly(self, Node, &Result);
        self->_hasAnchor = true;
        self->_anchor = Node;
        return ((Result.Code == ErrorCode_Success) && HasChanged) ? PostSelectionChange(widget, self) : Result;
    }
    if (args->Button != GameKey_MouseLeft)
    {
        return Error_CreateSuccess();
    }
    if (!HasRow)
    {
        bool IsKept = (args->Modifiers & (GAME_KEY_MODIFIER_CONTROL | GAME_KEY_MODIFIER_SHIFT)) != 0U;
        return (!IsKept && ClearAll(self)) ? PostSelectionChange(widget, self) : Error_CreateSuccess();
    }
    if (IsOnExpander(self, Row, InRow))
    {
        self->_hasPressNode = false; // The expander's press does the toggle alone: no selection, no drag.
        return ToggleRow(widget, self, Row);
    }
    UIListRowGeometry Geometry = GetRowGeometry(self);
    self->_pressCell = UIListRow_FindCell(&Geometry, self->Scrollable._viewSize.x, self->_metrics.RowHeight, self->_cellCount, InRow);
    if (self->_pressCell >= 0)
    {
        return Error_CreateSuccess();
    }
    if (args->ClickCount == 2U)
    {
        SetCurrent(self, Node, Row);
        Error Result = PostNodeEvent(widget, UIElementEvent_DoubleClicked, Node);
        return (Result.Code == ErrorCode_Success) ? PostNodeEvent(widget, UIElementEvent_Activated, Node) : Result;
    }
    return PressRow(widget, self, Row, args->Modifiers);
}

static Error OnButtonRelease(Widget* widget, UITreeCapability* self, const WidgetMouseInputArgs* args)
{
    if (!self->_hasPress || (args->Button != self->_pressButton))
    {
        return Error_CreateSuccess();
    }
    size_t Row = 0U;
    Vector2 InRow = { .x = 0.0f, .y = 0.0f };
    bool IsOverRow = TryGetRowAt(self, widget, args->WidgetPosition, &Row, &InRow);
    uint64_t Node = IsOverRow ? GetRowAt(self, Row)->Node : 0U;
    bool IsSameNode = IsOverRow && self->_hasPressNode && (Node == self->_pressNode);
    bool IsSameContext = IsOverRow && self->_hasContextNode && (Node == self->_contextNode);
    int32_t PressCell = self->_pressCell;
    bool IsSelectOnRelease = self->_isSelectOnRelease;
    ForgetPress(self);
    if (args->Button == GameKey_MouseRight)
    {
        return IsSameContext ? PostNodeEvent(widget, UIElementEvent_ContextRequested, Node) : Error_CreateSuccess();
    }
    if (!IsSameNode)
    {
        return Error_CreateSuccess();
    }
    if (PressCell >= 0)
    {
        UIListRowGeometry Geometry = GetRowGeometry(self);
        int32_t Cell = UIListRow_FindCell(&Geometry, self->Scrollable._viewSize.x, self->_metrics.RowHeight, self->_cellCount, InRow);
        if (Cell != PressCell)
        {
            return Error_CreateSuccess();
        }
        UIPropertyValue Value = UIPropertyValue_FromUInt64(Node);
        Error Result = UIScreen_PostWidgetIndexedEvent(Widget_GetScreen(widget), Widget_GetId(widget), UIElementEvent_CellToggled, &Value, Cell);
        return (Result.Code == ErrorCode_Success) ? UIElement_PostCue(widget, UIElementCue_Activate) : Result;
    }
    if (!IsSelectOnRelease)
    {
        return Error_CreateSuccess();
    }
    Error Result = Error_CreateSuccess();
    bool HasChanged = SelectOnly(self, Node, &Result);
    return ((Result.Code == ErrorCode_Success) && HasChanged) ? PostSelectionChange(widget, self) : Result;
}

static Error Behaviour_OnMouseInput(void* widget, const WidgetMouseInputArgs* args)
{
    UITreeCapability* Self = GetCapability(widget);
    Error Result = UIElement_PostMouseCues(widget, args, Self->_isEnabled);
    if ((Result.Code != ErrorCode_Success) || !Self->_isEnabled)
    {
        return Result;
    }
    UIScreen* Screen = Widget_GetScreen(widget);
    UIEventMouse Mouse = MakeMouse(args);
    switch (args->Type)
    {
        case UIMouseInputType_ButtonPress:
            Result = UIScreen_PostWidgetMouseEvent(Screen, Widget_GetId(widget), UIElementEvent_Pressed, &Mouse);
            return (Result.Code == ErrorCode_Success) ? OnButtonPress(widget, Self, args) : Result;
        case UIMouseInputType_ButtonRelease:
            Result = UIScreen_PostWidgetMouseEvent(Screen, Widget_GetId(widget), UIElementEvent_Released, &Mouse);
            return (Result.Code == ErrorCode_Success) ? OnButtonRelease(widget, Self, args) : Result;
        case UIMouseInputType_ReleaseOver:
            return UIScreen_PostWidgetMouseEvent(Screen, Widget_GetId(widget), UIElementEvent_Released, &Mouse);
        case UIMouseInputType_Move:
            return Error_CreateSuccess();
    }
    return Error_CreateSuccess();
}

static Error Behaviour_OnPressLost(void* widget, GameKey button)
{
    UITreeCapability* Self = GetCapability(widget);
    if (Self->_hasPress && (Self->_pressButton == button))
    {
        ForgetPress(Self);
    }
    return Error_CreateSuccess();
}

static Error Behaviour_OnScrollInput(void* widget, const WidgetScrollInputArgs* args, bool* outIsHandled)
{
    UITreeCapability* Self = GetCapability(widget);
    float Delta = -args->ScrollDelta.y * Self->Scrollable._wheelStep;
    if ((Delta == 0.0f) || !UIScrollableCapability_CanScroll(&Self->Scrollable, UIScrollAxis_Vertical, Delta))
    {
        return Error_CreateSuccess();
    }
    *outIsHandled = true;
    UIScrollableCapability_ScrollBy(&Self->Scrollable, (Vector2){ .x = 0.0f, .y = Delta });
    return Error_CreateSuccess();
}

/* Moves the current row by the keys, as a list's: alone the row reached is selected, with Shift the run from the anchor, with Ctrl only the
 * current row moves. */
static Error MoveCurrent(Widget* widget, UITreeCapability* self, int64_t target, uint8_t modifiers)
{
    size_t Count = GetRowCount(self);
    if (Count == 0U)
    {
        return Error_CreateSuccess();
    }
    int64_t Last = (int64_t)Count - 1;
    size_t Row = (size_t)((target < 0) ? 0 : ((target > Last) ? Last : target));
    uint64_t Node = GetRowAt(self, Row)->Node;
    bool IsControl = (modifiers & GAME_KEY_MODIFIER_CONTROL) != 0U;
    bool IsShift = (modifiers & GAME_KEY_MODIFIER_SHIFT) != 0U;
    Error Result = Error_CreateSuccess();
    bool HasChanged = false;
    if ((self->_mode == UISelectionMode_Multiple) && IsShift)
    {
        size_t From = Row;
        if (!(self->_hasAnchor && FindRowOfNode(self, self->_anchor, &From)))
        {
            From = self->_hasCurrent ? self->_currentRowHint : Row;
            self->_hasAnchor = true;
            self->_anchor = GetRowAt(self, Math_MinSizeT(From, Count - 1U))->Node;
        }
        HasChanged = SelectRows(self, From, Row, IsControl, &Result);
    }
    else if (!((self->_mode == UISelectionMode_Multiple) && IsControl) && (self->_mode != UISelectionMode_None))
    {
        HasChanged = SelectOnly(self, Node, &Result);
        self->_hasAnchor = true;
        self->_anchor = Node;
    }
    SetCurrent(self, Node, Row);
    RevealRow(self, Row);
    if ((Result.Code != ErrorCode_Success) || !HasChanged)
    {
        return Result;
    }
    Result = PostSelectionChange(widget, self);
    return (Result.Code == ErrorCode_Success) ? UIElement_PostCue(widget, UIElementCue_ValueStep) : Result;
}

static Error OnContextKey(Widget* widget, UITreeCapability* self, bool* outIsHandled)
{
    *outIsHandled = true;
    UIScreen* Screen = Widget_GetScreen(widget);
    Vector2 Middle = UIScreen_WidgetToScreen(Screen, widget, (Vector2){ .x = 0.5f, .y = 0.5f });
    size_t Row = 0U;
    if (self->_hasCurrent && FindRowOfNode(self, self->_current, &Row))
    {
        Error Result = PostNodeEvent(widget, UIElementEvent_ContextRequested, self->_current);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        Vector2 Box = GetBoxSize(widget);
        float RowMiddle = self->_metrics.Padding.y + (float)(((double)Row + 0.5) * (double)self->_metrics.RowHeight) - self->Scrollable._offset.y;
        if ((Box.y > 0.0f) && (RowMiddle >= 0.0f) && (RowMiddle <= Box.y))
        {
            Middle = UIScreen_WidgetToScreen(Screen, widget, (Vector2){ .x = 0.5f, .y = RowMiddle / Box.y });
        }
    }
    return UIContextMenu_Request(Screen, Widget_GetId(widget), Middle, true, NULL);
}

/* Left and Right: collapse or go to the parent; expand or go to the first child. */
static Error OnSideKey(Widget* widget, UITreeCapability* self, bool isRight, uint8_t modifiers)
{
    size_t Row = 0U;
    if (!self->_hasCurrent || !FindRowOfNode(self, self->_current, &Row))
    {
        return MoveCurrent(widget, self, 0, modifiers);
    }
    UITreeRow Current = *GetRowAt(self, Row);
    if (isRight)
    {
        if (!Current.HasChildren)
        {
            return Error_CreateSuccess();
        }
        return Current.IsExpanded ? MoveCurrent(widget, self, (int64_t)Row + 1, modifiers) : ToggleRow(widget, self, Row);
    }
    if (Current.HasChildren && Current.IsExpanded)
    {
        return ToggleRow(widget, self, Row);
    }
    for (size_t Above = Row; Above-- > 0U;)
    {
        if (GetRowAt(self, Above)->Depth < Current.Depth)
        {
            return MoveCurrent(widget, self, (int64_t)Above, modifiers);
        }
    }
    return Error_CreateSuccess();
}

static Error Behaviour_OnKeyboardInput(void* widget, const WidgetKeyboardInputArgs* args, bool* outIsHandled)
{
    UITreeCapability* Self = GetCapability(widget);
    UIScreen* Screen = Widget_GetScreen(widget);
    bool IsPress = args->Type == UIKeyboardInputType_KeyPress;
    bool IsDown = IsPress || (args->Type == UIKeyboardInputType_KeyRepeat);
    if (!Self->_isEnabled || !IsDown)
    {
        return Error_CreateSuccess();
    }
    if (IsPress && UIElement_IsActionPress(Screen, GameAction_UIContextMenu, args->Key))
    {
        return OnContextKey(widget, Self, outIsHandled);
    }
    if (!Widget_IsEntered(widget))
    {
        return Error_CreateSuccess();
    }
    size_t Row = 0U;
    int64_t Current = (Self->_hasCurrent && FindRowOfNode(Self, Self->_current, &Row)) ? (int64_t)Row : -1;
    int64_t Page = (int64_t)Math_MaxFloat(1.0f, Math_FloorFloat(Self->Scrollable._viewSize.y / Self->_metrics.RowHeight) - 1.0f);
    if (UIElement_IsActionKey(Screen, GameAction_UIUp, args->Key))
    {
        *outIsHandled = true;
        return MoveCurrent(widget, Self, (Current < 0) ? 0 : (Current - 1), args->Modifiers);
    }
    if (UIElement_IsActionKey(Screen, GameAction_UIDown, args->Key))
    {
        *outIsHandled = true;
        return MoveCurrent(widget, Self, Current + 1, args->Modifiers);
    }
    if (UIElement_IsActionKey(Screen, GameAction_UIRight, args->Key) || UIElement_IsActionKey(Screen, GameAction_UILeft, args->Key))
    {
        *outIsHandled = true;
        return OnSideKey(widget, Self, UIElement_IsActionKey(Screen, GameAction_UIRight, args->Key), args->Modifiers);
    }
    switch (args->Key)
    {
        case GameKey_PageUp:
            *outIsHandled = true;
            return MoveCurrent(widget, Self, (Current < 0) ? 0 : (Current - Page), args->Modifiers);
        case GameKey_PageDown:
            *outIsHandled = true;
            return MoveCurrent(widget, Self, (Current < 0) ? 0 : (Current + Page), args->Modifiers);
        case GameKey_Home:
            *outIsHandled = true;
            return MoveCurrent(widget, Self, 0, args->Modifiers);
        case GameKey_End:
            *outIsHandled = true;
            return MoveCurrent(widget, Self, (int64_t)GetRowCount(Self) - 1, args->Modifiers);
        default:
            break;
    }
    if (!IsPress)
    {
        return Error_CreateSuccess();
    }
    if ((args->Key == GameKey_Space) && (Current >= 0))
    {
        *outIsHandled = true;
        Error Result = Error_CreateSuccess();
        bool HasChanged = false;
        if ((Self->_mode == UISelectionMode_Multiple) && ((args->Modifiers & GAME_KEY_MODIFIER_CONTROL) != 0U))
        {
            HasChanged = true;
            if (ContainsId(&Self->_selection, Self->_current))
            {
                RemoveId(&Self->_selection, Self->_current);
            }
            else if (!AddId(&Self->_selection, Self->_current))
            {
                Result = SelectionGrowError();
            }
        }
        else if (Self->_mode != UISelectionMode_None)
        {
            HasChanged = SelectOnly(Self, Self->_current, &Result);
        }
        return ((Result.Code == ErrorCode_Success) && HasChanged) ? PostSelectionChange(widget, Self) : Result;
    }
    if (UIElement_IsActionPress(Screen, GameAction_UISelectAll, args->Key) && (Self->_mode == UISelectionMode_Multiple))
    {
        *outIsHandled = true;
        size_t Count = GetRowCount(Self);
        if (Count == 0U)
        {
            return Error_CreateSuccess();
        }
        Error Result = Error_CreateSuccess();
        bool HasChanged = SelectRows(Self, 0U, Count - 1U, true, &Result);
        return ((Result.Code == ErrorCode_Success) && HasChanged) ? PostSelectionChange(widget, Self) : Result;
    }
    if (UIElement_IsActionKey(Screen, GameAction_UIAccept, args->Key) && (Current >= 0))
    {
        *outIsHandled = true;
        Error Result = PostNodeEvent(widget, UIElementEvent_Activated, Self->_current);
        return (Result.Code == ErrorCode_Success) ? UIElement_PostCue(widget, UIElementCue_Activate) : Result;
    }
    return Error_CreateSuccess();
}

static Error Behaviour_OnEnter(void* widget, UIEnterCause cause)
{
    UITreeCapability* Self = GetCapability(widget);
    if ((cause != UIEnterCause_Press) && !Self->_hasCurrent && (GetRowCount(Self) > 0U))
    {
        size_t Row = 0U;
        for (size_t Index = 0U; Index < GetRowCount(Self); Index++)
        {
            if (ContainsId(&Self->_selection, GetRowAt(Self, Index)->Node))
            {
                Row = Index;
                break;
            }
        }
        SetCurrent(Self, GetRowAt(Self, Row)->Node, Row);
        Self->_isRevealPending = true;
    }
    return Error_CreateSuccess();
}


// Fields.
static const UIKindCapability TreeCapabilities[] =
{
    { .Capability = UIElementCapability_Tree, .Resolve = ResolveTreeCapability },
    { .Capability = UIElementCapability_Scrollable, .Resolve = ResolveScrollable }
};

static const UIKindBehaviour TreeBehaviour =
{
    .Kind = UIElementKind_Tree,
    .Capabilities = TreeCapabilities,
    .CapabilityCount = sizeof(TreeCapabilities) / sizeof(TreeCapabilities[0]),
    .BaseSize = sizeof(UITreeLookBase),
    .IsFocusable = true,
    .NeedsEntering = true,
    .EntersItself = false,
    .Construct = Behaviour_Construct,
    .Deconstruct = Behaviour_Deconstruct,
    .Update = Behaviour_Update,
    .Layout = Behaviour_Layout,
    .OnHoverStart = Behaviour_OnHoverStart,
    .OnMouseInput = Behaviour_OnMouseInput,
    .OnPressLost = Behaviour_OnPressLost,
    .OnScrollInput = Behaviour_OnScrollInput,
    .OnKeyboardInput = Behaviour_OnKeyboardInput,
    .OnEnter = Behaviour_OnEnter
};


// Static functions: drags.
static Error DragProvider(UIScreen* screen, uint64_t sourceId, Vector2 pressScreenPosition, const UserData* userData, UIDragPayload* outPayload,
    bool* outIsDragged)
{
    (void)userData;
    *outIsDragged = false;
    Widget* Target = NULL;
    UITreeCapability* Self = NULL;
    Error Result = ResolveTree(screen, sourceId, &Target, &Self);
    if ((Result.Code != ErrorCode_Success) || !Self->_isDragSource || !Self->_isEnabled)
    {
        Error_Deconstruct(&Result);
        return Error_CreateSuccess();
    }
    size_t Row = 0U;
    Vector2 InRow = { .x = 0.0f, .y = 0.0f };
    if (!TryGetRowAt(Self, Target, UIScreen_ScreenToWidget(screen, Target, pressScreenPosition), &Row, &InRow)
        || !ContainsId(&Self->_selection, GetRowAt(Self, Row)->Node))
    {
        return Error_CreateSuccess(); // Only selected nodes go.
    }
    // The expander's press toggles alone (the press keeps no node), and a toggle cell's toggles at its release: neither drags.
    UIListRowGeometry Geometry = GetRowGeometry(Self);
    if (!Self->_hasPressNode || IsOnExpander(Self, Row, InRow)
        || (UIListRow_FindCell(&Geometry, Self->Scrollable._viewSize.x, Self->_metrics.RowHeight, Self->_cellCount, InRow) >= 0))
    {
        return Error_CreateSuccess();
    }
    uint64_t Node = GetRowAt(Self, Row)->Node;
    outPayload->Kind = Self->_dragKind;
    outPayload->Value = Node;
    if (Self->_selection._count > 1U)
    {
        Result = UIDragPayload_SetTextCount(outPayload, Self->_selection._count, (const unsigned char*)u8"items");
    }
    else
    {
        UIListRowData Data = { .Text = &Self->_scratch, .Icon = UIElementIcon_None(), .IsDimmed = false };
        GenericBuffer_Clear(&Self->_scratch);
        Result = (Self->_provider != NULL) ? Self->_provider->WriteNode(screen, sourceId, &Self->_providerData, Node, &Data) : Error_CreateSuccess();
        if ((Result.Code == ErrorCode_Success) && UIScreen_TryGetWidgetById(screen, sourceId, &Target))
        {
            Self = GetCapability(Target);
            Result = UIDragPayload_SetText(outPayload, Self->_scratch._data, Self->_scratch._count);
        }
    }
    *outIsDragged = Result.Code == ErrorCode_Success;
    return Result;
}


// Internal functions (declared in UIKitInternal.h).
const UIKindBehaviour* UITree_GetKindBehaviour(void)
{
    return &TreeBehaviour;
}


// Public functions: helpers.
UITreeOptions UITreeOptions_Create(void)
{
    UITreeOptions Options =
    {
        .Element = UIElementOptions_Create(),
        .SelectionMode = UISelectionMode_Multiple,
        .Provider = NULL,
        .ProviderData = UserData_CreateEmpty(),
        .CellCount = 0U,
        .VisibleRows = UI_LIST_DEFAULT_VISIBLE_ROWS,
        .IsEnabled = true,
        .IsDragSource = false,
        .DragKind = 0U
    };
    for (size_t Index = 0U; Index < UI_LIST_MAX_CELLS; Index++)
    {
        Options.CellIcons[Index] = UIElementIcon_None();
    }
    return Options;
}


// Public functions: calling code.
Error UITree_SetProvider(UIScreen* screen, uint64_t widgetId, const UITreeProvider* provider, const UserData* userData)
{
    if ((provider != NULL) && !IsProviderWhole(provider))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UITree_SetProvider: a provider needs every hook.");
    }
    Widget* Target = NULL;
    UITreeCapability* Self = NULL;
    Error Result = ResolveTree(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Self->_provider = provider;
    Self->_providerData = (userData != NULL) ? *userData : UserData_CreateEmpty();
    ClearAll(Self);
    Self->_hasCurrent = false;
    Self->_hasAnchor = false;
    Self->_hasHovered = false;
    ForgetPress(Self);
    Self->_isDirty = true;
    Self->_isSelectionCheckPending = false;
    return Error_CreateSuccess();
}

Error UITree_Refresh(UIScreen* screen, uint64_t widgetId)
{
    Widget* Target = NULL;
    UITreeCapability* Self = NULL;
    Error Result = ResolveTree(screen, widgetId, &Target, &Self);
    if (Result.Code == ErrorCode_Success)
    {
        Self->_isDirty = true;
        Self->_isSelectionCheckPending = true;
    }
    return Result;
}

Error UITree_SetExpanded(UIScreen* screen, uint64_t widgetId, uint64_t node, bool isExpanded)
{
    Widget* Target = NULL;
    UITreeCapability* Self = NULL;
    Error Result = ResolveTree(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (Self->_provider == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UITree_SetExpanded: the tree has no data.");
    }
    Self->_provider->SetExpanded(screen, widgetId, &Self->_providerData, node, isExpanded);
    if (UIScreen_TryGetWidgetById(screen, widgetId, &Target))
    {
        GetCapability(Target)->_isDirty = true;
    }
    return Error_CreateSuccess();
}

Error UITree_SetSelected(UIScreen* screen, uint64_t widgetId, uint64_t node, bool isSelected)
{
    Widget* Target = NULL;
    UITreeCapability* Self = NULL;
    Error Result = ResolveTree(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!isSelected)
    {
        RemoveId(&Self->_selection, node);
        return Error_CreateSuccess();
    }
    if (Self->_mode == UISelectionMode_None)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UITree_SetSelected: the tree selects nothing.");
    }
    if (Self->_mode == UISelectionMode_Single)
    {
        ClearAll(Self);
    }
    return AddId(&Self->_selection, node) ? Error_CreateSuccess() : SelectionGrowError();
}

Error UITree_ClearSelection(UIScreen* screen, uint64_t widgetId)
{
    Widget* Target = NULL;
    UITreeCapability* Self = NULL;
    Error Result = ResolveTree(screen, widgetId, &Target, &Self);
    if (Result.Code == ErrorCode_Success)
    {
        ClearAll(Self);
    }
    return Result;
}

Error UITree_IsSelected(UIScreen* screen, uint64_t widgetId, uint64_t node, bool* outIsSelected)
{
    Widget* Target = NULL;
    UITreeCapability* Self = NULL;
    Error Result = ResolveTree(screen, widgetId, &Target, &Self);
    if (Result.Code == ErrorCode_Success)
    {
        *outIsSelected = ContainsId(&Self->_selection, node);
    }
    return Result;
}

Error UITree_GetSelectedCount(UIScreen* screen, uint64_t widgetId, size_t* outCount)
{
    Widget* Target = NULL;
    UITreeCapability* Self = NULL;
    Error Result = ResolveTree(screen, widgetId, &Target, &Self);
    if (Result.Code == ErrorCode_Success)
    {
        *outCount = Self->_selection._count;
    }
    return Result;
}

Error UITree_GetSelectedAt(UIScreen* screen, uint64_t widgetId, size_t index, uint64_t* outNode)
{
    Widget* Target = NULL;
    UITreeCapability* Self = NULL;
    Error Result = ResolveTree(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (index >= Self->_selection._count)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UITree_GetSelectedAt: past the count.");
    }
    *outNode = GetIdAt(&Self->_selection, index);
    return Error_CreateSuccess();
}

Error UITree_SetCurrent(UIScreen* screen, uint64_t widgetId, uint64_t node)
{
    Widget* Target = NULL;
    UITreeCapability* Self = NULL;
    Error Result = ResolveTree(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    size_t Row = 0U;
    SetCurrent(Self, node, FindRowOfNode(Self, node, &Row) ? Row : 0U);
    Self->_hasAnchor = true;
    Self->_anchor = node;
    Self->_isRevealPending = true;
    return Error_CreateSuccess();
}

Error UITree_GetCurrent(UIScreen* screen, uint64_t widgetId, bool* outHasCurrent, uint64_t* outNode)
{
    Widget* Target = NULL;
    UITreeCapability* Self = NULL;
    Error Result = ResolveTree(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    *outHasCurrent = Self->_hasCurrent;
    if (outNode != NULL)
    {
        *outNode = Self->_current;
    }
    return Error_CreateSuccess();
}

Error UITree_GetRowCount(UIScreen* screen, uint64_t widgetId, size_t* outCount)
{
    Widget* Target = NULL;
    UITreeCapability* Self = NULL;
    Error Result = ResolveTree(screen, widgetId, &Target, &Self);
    if (Result.Code == ErrorCode_Success)
    {
        *outCount = GetRowCount(Self);
    }
    return Result;
}

Error UITree_GetRow(UIScreen* screen, uint64_t widgetId, size_t index, UITreeRow* outRow)
{
    Widget* Target = NULL;
    UITreeCapability* Self = NULL;
    Error Result = ResolveTree(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (index >= GetRowCount(Self))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UITree_GetRow: past the count.");
    }
    *outRow = *GetRowAt(Self, index);
    return Error_CreateSuccess();
}

Error UITree_GetItemAt(UIScreen* screen, uint64_t widgetId, Vector2 screenPosition, bool* outHasNode, uint64_t* outNode)
{
    Widget* Target = NULL;
    UITreeCapability* Self = NULL;
    Error Result = ResolveTree(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    size_t Row = 0U;
    *outHasNode = TryGetRowAt(Self, Target, UIScreen_ScreenToWidget(screen, Target, screenPosition), &Row, NULL);
    if (outNode != NULL)
    {
        *outNode = *outHasNode ? GetRowAt(Self, Row)->Node : 0U;
    }
    return Error_CreateSuccess();
}

Error UITree_GetDropPlace(UIScreen* screen, uint64_t widgetId, Vector2 screenPosition, uint64_t* outNode, UITreeDropEdge* outEdge)
{
    Widget* Target = NULL;
    UITreeCapability* Self = NULL;
    Error Result = ResolveTree(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Vector2 Content = ToContent(Self, Target, UIScreen_ScreenToWidget(screen, Target, screenPosition));
    size_t Row = 0U;
    *outEdge = GetDropPlace(Self, Content.y, &Row);
    *outNode = (*outEdge != UITreeDropEdge_None) ? GetRowAt(Self, Row)->Node : 0U;
    return Error_CreateSuccess();
}

Error UITree_SetSelectionMode(UIScreen* screen, uint64_t widgetId, UISelectionMode mode)
{
    if (!UISelectionMode_IsValid(mode))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UITree_SetSelectionMode: an unknown mode.");
    }
    Widget* Target = NULL;
    UITreeCapability* Self = NULL;
    Error Result = ResolveTree(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Self->_mode = mode;
    if (mode == UISelectionMode_None)
    {
        ClearAll(Self);
    }
    else if ((mode == UISelectionMode_Single) && (Self->_selection._count > 1U))
    {
        GenericBuffer_SetCount(&Self->_selection, 1U);
    }
    return Error_CreateSuccess();
}

Error UITree_SetDragSource(UIScreen* screen, uint64_t widgetId, bool isDragSource, uint32_t dragKind)
{
    Widget* Target = NULL;
    UITreeCapability* Self = NULL;
    Error Result = ResolveTree(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Self->_isDragSource = isDragSource;
    Self->_dragKind = dragKind;
    if (!isDragSource)
    {
        UIDragDrop_ClearSource(screen, widgetId);
        return Error_CreateSuccess();
    }
    return UIDragDrop_SetSource(screen, widgetId, DragProvider, NULL);
}

Error UITree_SetIsEnabled(UIScreen* screen, uint64_t widgetId, bool isEnabled)
{
    Widget* Target = NULL;
    UITreeCapability* Self = NULL;
    Error Result = ResolveTree(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Self->_isEnabled = isEnabled;
    if (!isEnabled)
    {
        ForgetPress(Self);
    }
    return Widget_SetIsFocusable(Target, isEnabled);
}


// Public functions: behaviour helpers.
void UITreeCapability_SetMetrics(UITreeCapability* self, Widget* widget, const UITreeMetrics* metrics)
{
    UITreeMetrics Clean =
    {
        .Padding = { .x = CleanLength(metrics->Padding.x), .y = CleanLength(metrics->Padding.y) },
        .RowHeight = Math_MaxFloat(1.0f, CleanLength(metrics->RowHeight)),
        .RowPadding = CleanLength(metrics->RowPadding),
        .Indent = CleanLength(metrics->Indent),
        .CellSize = CleanLength(metrics->CellSize),
        .InsertionThickness = CleanLength(metrics->InsertionThickness)
    };
    bool IsSame = (Clean.Padding.x == self->_metrics.Padding.x) && (Clean.Padding.y == self->_metrics.Padding.y)
        && (Clean.RowHeight == self->_metrics.RowHeight) && (Clean.RowPadding == self->_metrics.RowPadding) && (Clean.Indent == self->_metrics.Indent)
        && (Clean.CellSize == self->_metrics.CellSize) && (Clean.InsertionThickness == self->_metrics.InsertionThickness);
    if (IsSame)
    {
        return;
    }
    self->_metrics = Clean;
    Widget_InvalidateMeasure(widget);
}

UIElementState UITreeCapability_GetState(const UITreeCapability* self, const Widget* widget)
{
    return UIElementState_Choose(self->_isEnabled, false, false, Widget_IsEntered(widget), false, Widget_IsHovered(widget),
        Widget_IsFocused(widget));
}

void UITreeCapability_Measure(const UITreeCapability* self, Vector2* outSize)
{
    const UITreeMetrics* Metrics = &self->_metrics;
    float Cells = (float)self->_cellCount * (Metrics->CellSize + Metrics->RowPadding);
    float Indent = (TREE_MEASURED_LEVELS + 1.0f) * Metrics->Indent;
    outSize->x = (2.0f * Metrics->Padding.x) + (2.0f * Metrics->RowPadding) + Indent + TREE_MIN_TEXT_WIDTH + Cells + self->_barThickness;
    outSize->y = (2.0f * Metrics->Padding.y) + ((float)self->_visibleRows * Metrics->RowHeight);
}

void UITree_SetParts(UITreeCapability* self, uint64_t rowsBoxId, uint64_t barId)
{
    self->_rowsBoxId = rowsBoxId;
    self->_barId = barId;
}
