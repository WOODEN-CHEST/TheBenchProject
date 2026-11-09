#include "UIList.h"
#include "UIContextMenu.h"
#include "UIDragDrop.h"
#include "UIKit.h"
#include "UIKitInternal.h"
#include "UIListRow.h"
#include "UIScreen.h"
#include "UIScrollbar.h"
#include "wr/WRMath.h"
#include "wr/WRString.h"


// Macros.
/** The narrowest a list's text column measures, in UI pixels. */
#define LIST_MIN_TEXT_WIDTH 96.0f
/** How far, in UI pixels, the rows may pass the view and still fit: float noise shows no bar. */
#define FIT_TOLERANCE 0.01f
/** The selection's runs a list starts with room for. */
#define INITIAL_RANGE_CAPACITY ((size_t)8)


// Static functions: the selection's runs.
static UIListRange* GetRangeAt(GenericBuffer* ranges, size_t index)
{
    return GenericBuffer_GetPointerToElement(ranges, index);
}

static const UIListRange* PeekRangeAt(const GenericBuffer* ranges, size_t index)
{
    return GenericBuffer_GetPointerToElement((GenericBuffer*)ranges, index);
}

static size_t GetRangeEnd(const UIListRange* range)
{
    return range->Start + range->Count;
}

/* The first run that ends after @p index (its end past it), or the run count. */
static size_t FindFirstEndingAfter(const GenericBuffer* ranges, size_t index)
{
    size_t Low = 0U;
    size_t High = ranges->_count;
    while (Low < High)
    {
        size_t Middle = Low + ((High - Low) / 2U);
        if (GetRangeEnd(PeekRangeAt(ranges, Middle)) > index)
        {
            High = Middle;
        }
        else
        {
            Low = Middle + 1U;
        }
    }
    return Low;
}

/* Merges the run at @p index with the one after it while they touch. */
static void MergeForward(GenericBuffer* ranges, size_t index)
{
    while ((index + 1U) < ranges->_count)
    {
        UIListRange* This = GetRangeAt(ranges, index);
        const UIListRange* Next = PeekRangeAt(ranges, index + 1U);
        if (GetRangeEnd(This) < Next->Start)
        {
            return;
        }
        size_t End = Math_MaxSizeT(GetRangeEnd(This), GetRangeEnd(Next));
        This->Count = End - This->Start;
        GenericBuffer_RemoveAt(ranges, index + 1U);
    }
}


// Static functions: helpers.
static UIListCapability* GetCapability(void* widget)
{
    return &((UIListLookBase*)widget)->List;
}

static Error ResolveList(UIScreen* screen, uint64_t widgetId, Widget** outWidget, UIListCapability** outList)
{
    UIKit* Kit = (screen != NULL) ? UIKit_FromScreen(screen) : NULL;
    Widget* Target = NULL;
    if ((Kit == NULL) || !UIScreen_TryGetWidgetById(screen, widgetId, &Target))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIList: the widget is gone, or its screen has no kit.");
    }
    UIListCapability* List = Widget_GetCapability(Target, UIKit_GetCapabilityId(Kit, UIElementCapability_List));
    if (List == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIList: the widget is not a list.");
    }
    *outWidget = Target;
    *outList = List;
    return Error_CreateSuccess();
}

/* A length as a list keeps it: a finite number >= 0, else 0. */
static float CleanLength(float value)
{
    return (Math_IsNaNFloat(value) || Math_IsInfinityFloat(value) || !(value > 0.0f)) ? 0.0f : value;
}

static UIListRowGeometry GetRowGeometry(const UIListCapability* self)
{
    return (UIListRowGeometry){ .RowPadding = self->_metrics.RowPadding, .CellSize = self->_metrics.CellSize,
        .InsertionThickness = self->_metrics.InsertionThickness, .Indent = 0.0f, .HasExpanderColumn = false };
}

/* The list's box in its own UI pixels, as last laid out. */
static Vector2 GetBoxSize(const Widget* widget)
{
    Rectangle Pixels = UIScreen_GetWidgetPixelBox(Widget_GetScreen(widget), widget);
    float Scale = Widget_GetEffectiveScale(widget);
    return (Vector2){ .x = Pixels.width / Scale, .y = Pixels.height / Scale };
}

/* A point of the list (normalized to its box) in its rows' space: x across the rows' box, y down the content (the offset added). */
static Vector2 ToContent(const UIListCapability* self, const Widget* widget, Vector2 widgetPosition)
{
    Vector2 Box = GetBoxSize(widget);
    return (Vector2){ .x = (widgetPosition.x * Box.x) - self->_metrics.Padding.x,
        .y = (widgetPosition.y * Box.y) - self->_metrics.Padding.y + self->Scrollable._offset.y };
}

/* The row at a point of the content, if any: inside the rows' box across and down, and below the count. */
static bool TryGetRowAt(const UIListCapability* self, const Widget* widget, Vector2 widgetPosition, size_t* outRow, Vector2* outInRow)
{
    Vector2 Content = ToContent(self, widget, widgetPosition);
    float ViewTop = self->Scrollable._offset.y;
    bool IsInside = (Content.x >= 0.0f) && (Content.x < self->Scrollable._viewSize.x) && (Content.y >= ViewTop)
        && (Content.y < (ViewTop + self->Scrollable._viewSize.y));
    if (!IsInside || !(self->_metrics.RowHeight > 0.0f))
    {
        return false;
    }
    double Row = Math_FloorDouble((double)Content.y / (double)self->_metrics.RowHeight);
    if ((Row < 0.0) || !(Row < (double)self->_count))
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

static UIEventMouse MakeMouse(const Widget* widget, const WidgetMouseInputArgs* args)
{
    bool IsOver = (args->WidgetPosition.x >= 0.0f) && (args->WidgetPosition.x <= 1.0f) && (args->WidgetPosition.y >= 0.0f)
        && (args->WidgetPosition.y <= 1.0f);
    (void)widget;
    return (UIEventMouse){ .Button = args->Button, .Modifiers = args->Modifiers, .ScreenPosition = args->ScreenPosition,
        .ScrollDelta = { .x = 0.0f, .y = 0.0f }, .IsOverWidget = IsOver || (args->Type == UIMouseInputType_ReleaseOver) };
}

static Error PostItemEvent(Widget* widget, UIElementEvent event, size_t row)
{
    UIPropertyValue Value = UIPropertyValue_FromUInt64((uint64_t)row);
    return UIScreen_PostWidgetEvent(Widget_GetScreen(widget), Widget_GetId(widget), (int32_t)event, &Value);
}

/* The user changed the selection: one whole change, its value the number of rows selected. */
static Error PostSelectionChange(Widget* widget, const UIListCapability* self)
{
    UIScreen* Screen = Widget_GetScreen(widget);
    UIPropertyValue Value = UIPropertyValue_FromDouble((double)self->_selectedCount);
    Error Result = UIScreen_PostWidgetEvent(Screen, Widget_GetId(widget), UIElementEvent_Changed, &Value);
    return (Result.Code == ErrorCode_Success) ? UIScreen_PostWidgetEvent(Screen, Widget_GetId(widget), UIElementEvent_Committed, &Value) : Result;
}

static Error SelectionGrowError(void)
{
    return Error_Construct2(ErrorCode_BufferTooLarge, "UIList: the selection could not grow.");
}

/* Deselects everything. Returns whether anything was selected. */
static bool ClearAll(UIListCapability* self)
{
    bool HadAny = self->_selectedCount > 0U;
    GenericBuffer_Clear(&self->_selection);
    self->_selectedCount = 0U;
    return HadAny;
}

/* Selects only one row. Returns whether the selection changed; false with *outResult set when it could not grow. */
static bool SelectOnly(UIListCapability* self, size_t row, Error* outResult)
{
    if ((self->_selectedCount == 1U) && UIListRanges_Contains(&self->_selection, row))
    {
        return false;
    }
    ClearAll(self);
    if (!UIListRanges_Set(&self->_selection, row, 1U, true, &self->_selectedCount))
    {
        *outResult = SelectionGrowError();
    }
    return true;
}

/* Selects the rows from @p from to @p to, both in, in either order; @p isAdding keeps what was selected. */
static bool SelectSpan(UIListCapability* self, size_t from, size_t to, bool isAdding, Error* outResult)
{
    size_t First = Math_MinSizeT(from, to);
    size_t Count = Math_MaxSizeT(from, to) - First + 1U;
    size_t Before = self->_selectedCount;
    size_t RangesBefore = self->_selection._count;
    bool WasExactly = (RangesBefore == 1U) && (PeekRangeAt(&self->_selection, 0U)->Start == First)
        && (PeekRangeAt(&self->_selection, 0U)->Count == Count);
    if (!isAdding)
    {
        if (WasExactly)
        {
            return false;
        }
        ClearAll(self);
    }
    if (!UIListRanges_Set(&self->_selection, First, Count, true, &self->_selectedCount))
    {
        *outResult = SelectionGrowError();
        return true;
    }
    return !isAdding || (self->_selectedCount != Before);
}

static void SetCurrent(UIListCapability* self, size_t row)
{
    self->_hasCurrent = true;
    self->_current = row;
}

/* Scrolls as little as it takes to show a row, by the view of the last layout. */
static void RevealRow(UIListCapability* self, size_t row)
{
    float RowHeight = self->_metrics.RowHeight;
    Rectangle Box = { .x = 0.0f, .y = (float)((double)row * (double)RowHeight), .width = 1.0f, .height = RowHeight };
    UIScrollableCapability_EnsureVisible(&self->Scrollable, Box);
}

/* Forgets what lies past the count: selected rows, the current row, the anchor, the hovered row, a press. */
static void ClampToCount(UIListCapability* self)
{
    size_t Count = self->_count;
    UIListRanges_Remove(&self->_selection, Count, SIZE_MAX - Count, &self->_selectedCount);
    if (self->_hasCurrent && (self->_current >= Count))
    {
        self->_hasCurrent = Count > 0U;
        self->_current = (Count > 0U) ? (Count - 1U) : 0U;
    }
    self->_hasAnchor = self->_hasAnchor && (self->_anchor < Count);
    self->_hasHovered = self->_hasHovered && (self->_hovered < Count);
    self->_hasPressRow = self->_hasPressRow && (self->_pressRow < Count);
    self->_hasContextRow = self->_hasContextRow && (self->_contextRow < Count);
    self->_hasInsertion = self->_hasInsertion && (self->_insertion <= Count);
}

/* Moves a row index for rows inserted: from @p index on, down by @p count. */
static void ShiftForInsert(bool hasRow, size_t* row, size_t index, size_t count)
{
    if (hasRow && (*row >= index))
    {
        *row += count;
    }
}

/* Moves a row index for rows removed; returns false when the row itself went. */
static bool ShiftForRemove(size_t* row, size_t index, size_t count)
{
    if (*row < index)
    {
        return true;
    }
    if (*row < (index + count))
    {
        return false;
    }
    *row -= count;
    return true;
}


// Static functions: the rows.
static Error Rows_Create(void* owner, uint64_t* outWidgetId)
{
    Widget* Base = owner;
    UIListCapability* Self = GetCapability(owner);
    UIScreen* Screen = Widget_GetScreen(Base);
    UIKit* Kit = UIKit_FromScreen(Screen);
    const UIElementLook* Look = Widget_GetCapability(Base, UIKit_GetCapabilityId(Kit, UIElementCapability_Look));
    const unsigned char* Variant = ((Look != NULL) && (Look->_variant[0] != 0U)) ? Look->_variant : NULL;
    return UIListRow_Create(Screen, Kit, Self->_rowsBoxId, UIListRow_GetListParts(), Variant, outWidgetId);
}

static Error Rows_Bind(void* owner, uint64_t widgetId, size_t dataIndex)
{
    Widget* Base = owner;
    UIListCapability* Self = GetCapability(owner);
    UIScreen* Screen = Widget_GetScreen(Base);
    UIListRowData Data = { .Text = &Self->_scratch, .Icon = UIElementIcon_None(), .IsDimmed = false };
    for (size_t Index = 0U; Index < UI_LIST_MAX_CELLS; Index++)
    {
        Data.Cells[Index] = false;
    }
    GenericBuffer_Clear(&Self->_scratch);
    uint64_t ListId = Widget_GetId(Base);
    if (Self->_rowProvider != NULL)
    {
        Error Result = Self->_rowProvider(Screen, ListId, &Self->_providerData, dataIndex, &Data);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    // The provider ran: the list is found again.
    Widget* Found = NULL;
    if (!UIScreen_TryGetWidgetById(Screen, ListId, &Found))
    {
        return Error_CreateSuccess();
    }
    Self = GetCapability(Found);
    // Its text ends at its first NUL, cut at a codepoint within the most a row shows.
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
        return Error_Construct2(ErrorCode_BufferTooLarge, "UIList: a row's text could not be held.");
    }
    UIListRowContent Content = { .Text = Self->_scratch._data, .Icon = Data.Icon, .IsDimmed = Data.IsDimmed, .CellCount = Self->_cellCount,
        .CellIcons = Self->_cellIcons, .Cells = Data.Cells, .Depth = 0U, .HasChildren = false, .IsExpanded = false };
    return UIListRow_Bind(Screen, widgetId, &Content);
}

static Error Rows_Unbind(void* owner, uint64_t widgetId)
{
    UIListRow_Unbind(Widget_GetScreen((Widget*)owner), widgetId);
    return Error_CreateSuccess();
}

static const UIRowRecyclerHooks RowHooks = { .CreateRow = Rows_Create, .BindRow = Rows_Bind, .UnbindRow = Rows_Unbind };

/* Tells each bound row how it stands this update. */
static void PushRowViews(UIListCapability* self, Widget* widget)
{
    UIScreen* Screen = Widget_GetScreen(widget);
    UIListRowGeometry Geometry = GetRowGeometry(self);
    bool IsMarked = Widget_IsFocused(widget) && self->_hasCurrent;
    UIDragTargetState Target = UIDragDrop_GetTargetState(Screen, Widget_GetId(widget));
    for (size_t Index = 0U; Index < UIRowRecycler_GetCount(&self->_rows); Index++)
    {
        const UIRecycledRow* Row = UIRowRecycler_GetAt(&self->_rows, Index);
        UIListRowView View = { .IsEnabled = self->_isEnabled, .IsSelected = false, .IsHovered = false, .IsCurrentShown = false,
            .IsOwnerEntered = Widget_IsEntered(widget), .HoveredCell = -1, .PressedCell = -1, .IsExpanderHovered = false,
            .Insertion = UIListRowInsertion_None };
        if (Row->IsBound)
        {
            size_t Data = Row->DataIndex;
            View.IsSelected = UIListRanges_Contains(&self->_selection, Data);
            View.IsHovered = self->_hasHovered && (self->_hovered == Data);
            View.IsCurrentShown = IsMarked && (self->_current == Data);
            View.HoveredCell = View.IsHovered ? self->_hoveredCell : -1;
            View.PressedCell = (self->_hasPressRow && (self->_pressRow == Data)) ? self->_pressCell : -1;
            if ((Target == UIDragTargetState_Accepting) && self->_hasInsertion)
            {
                if (self->_insertion == Data)
                {
                    View.Insertion = UIListRowInsertion_Above;
                }
                else if ((self->_insertion == (Data + 1U)) && (self->_insertion == self->_count))
                {
                    View.Insertion = UIListRowInsertion_Below;
                }
            }
        }
        UIListRow_SetView(Screen, Row->WidgetId, &View, &Geometry);
    }
}

/* What lies under the pointer: the row and its cell, while the pointer is over the list itself and no drag is on. */
static void TrackHover(UIListCapability* self, Widget* widget)
{
    UIScreen* Screen = Widget_GetScreen(widget);
    self->_hasHovered = false;
    self->_hoveredCell = -1;
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
        self->_hovered = Row;
        self->_hoveredCell = UIListRow_FindCell(&Geometry, self->Scrollable._viewSize.x, self->_metrics.RowHeight, self->_cellCount, InRow);
    }
}

/* While a drag would drop on the list: where it would go, and the view scrolled when the pointer nears its top or bottom. */
static void TrackDrag(UIListCapability* self, Widget* widget, double passedSeconds)
{
    UIScreen* Screen = Widget_GetScreen(widget);
    UIDragTargetState Target = UIDragDrop_GetTargetState(Screen, Widget_GetId(widget));
    self->_hasInsertion = false;
    if (Target == UIDragTargetState_None)
    {
        self->_dragScrollSeconds = 0.0;
        return;
    }
    UIDragSession Session;
    if (!UIDragDrop_GetSession(Screen, &Session))
    {
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
        double Place = Math_FloorDouble(((double)Content.y / (double)self->_metrics.RowHeight) + 0.5);
        self->_hasInsertion = true;
        self->_insertion = (Place <= 0.0) ? 0U : ((Place >= (double)self->_count) ? self->_count : (size_t)Place);
    }
}


// Static functions: the behaviour.
static void* ResolveListCapability(void* widget)
{
    return GetCapability(widget);
}

static void* ResolveScrollable(void* widget)
{
    return &GetCapability(widget)->Scrollable;
}

static bool AreOptionsValid(const UIListOptions* options)
{
    bool IsValid = UISelectionMode_IsValid(options->SelectionMode) && (options->CellCount <= UI_LIST_MAX_CELLS)
        && (options->VisibleRows >= 1U) && (options->VisibleRows <= UI_LIST_MAX_VISIBLE_ROWS)
        && ((options->CountProvider == NULL) == (options->RowProvider == NULL));
    for (size_t Index = 0U; IsValid && (Index < options->CellCount); Index++)
    {
        IsValid = UIElementIcon_IsValid(&options->CellIcons[Index]);
    }
    return IsValid;
}

/* The provider of a drag: the selection goes, named by the pressed row. */
static Error DragProvider(UIScreen* screen, uint64_t sourceId, Vector2 pressScreenPosition, const UserData* userData, UIDragPayload* outPayload,
    bool* outIsDragged);

static Error Behaviour_Construct(void* widget, void* args)
{
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIList: the arguments are required.");
    }
    const UIListOptions* Options = ((const UIListArgs*)args)->Options;
    if (!AreOptionsValid(Options))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "UIList: a known selection mode, at most UI_LIST_MAX_CELLS valid cells, a visible row count within [1, UI_LIST_MAX_VISIBLE_ROWS] and both providers or neither are required.");
    }
    Error Result = UIElement_ApplyOptions(widget, &Options->Element);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    UIListCapability* Self = GetCapability(widget);
    UIScrollableCapability_Construct(&Self->Scrollable, UIScrollMode_Disabled, UIScrollMode_Auto, 22.0f * UI_LIST_WHEEL_ROWS);
    Self->_countProvider = Options->CountProvider;
    Self->_rowProvider = Options->RowProvider;
    Self->_providerData = Options->ProviderData;
    Self->_count = 0U;
    Self->_mode = Options->SelectionMode;
    GenericBuffer_AllocateVariable(&Self->_selection, INITIAL_RANGE_CAPACITY, sizeof(UIListRange));
    Self->_selectedCount = 0U;
    Self->_hasCurrent = false;
    Self->_current = 0U;
    Self->_hasAnchor = false;
    Self->_anchor = 0U;
    Self->_hasHovered = false;
    Self->_hovered = 0U;
    Self->_hoveredCell = -1;
    UIRowRecycler_Construct(&Self->_rows);
    Self->_rowsBoxId = WIDGET_ID_INVALID;
    Self->_barId = WIDGET_ID_INVALID;
    Self->_metrics = (UIListMetrics){ .Padding = { .x = 1.0f, .y = 1.0f }, .RowHeight = 22.0f, .RowPadding = 6.0f, .CellSize = 14.0f,
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
    Self->_hasPress = false;
    Self->_pressButton = GameKey_Unknown;
    Self->_hasPressRow = false;
    Self->_pressRow = 0U;
    Self->_pressCell = -1;
    Self->_isSelectOnRelease = false;
    Self->_hasContextRow = false;
    Self->_contextRow = 0U;
    Self->_dragScrollSeconds = 0.0;
    Self->_hasInsertion = false;
    Self->_insertion = 0U;
    if (!Options->IsEnabled)
    {
        Error Focus = Widget_SetIsFocusable(widget, false); // The widget is not alive yet: nothing can fail or run.
        Error_Deconstruct(&Focus);
    }
    return Error_CreateSuccess();
}

static Error Behaviour_Deconstruct(void* widget)
{
    UIListCapability* Self = GetCapability(widget);
    Memory_Free(Self->_selection._data);
    Memory_Free(Self->_scratch._data);
    UIRowRecycler_Deconstruct(&Self->_rows);
    return Error_CreateSuccess();
}

static Error Behaviour_Update(void* widget, ProgramTime time)
{
    UIListCapability* Self = GetCapability(widget);
    UIScreen* Screen = Widget_GetScreen(widget);
    uint64_t ListId = Widget_GetId(widget);
    if (!UIScreen_IsWidgetAlive(Screen, Self->_rowsBoxId))
    {
        return Error_CreateSuccess(); // Its parts come once it is attached (UIKit_CreateList).
    }
    size_t Count = (Self->_countProvider != NULL) ? Self->_countProvider(Screen, ListId, &Self->_providerData) : 0U;
    Widget* Found = NULL;
    if (!UIScreen_TryGetWidgetById(Screen, ListId, &Found))
    {
        return Error_CreateSuccess();
    }
    Self = GetCapability(Found);
    if (Count != Self->_count)
    {
        // The data changed with no word: what lies past the count goes, and every row is asked again.
        Self->_count = Count;
        ClampToCount(Self);
        UIRowRecycler_Invalidate(&Self->_rows);
    }
    TrackHover(Self, Found);
    TrackDrag(Self, Found, time.PassedTime);
    Error Result = UIRowRecycler_Sync(&Self->_rows, Screen, Self->_count, Self->_metrics.RowHeight, Self->Scrollable._offset.y,
        Self->Scrollable._viewSize.y, &RowHooks, Found);
    if ((Result.Code != ErrorCode_Success) || !UIScreen_TryGetWidgetById(Screen, ListId, &Found))
    {
        return Result;
    }
    PushRowViews(GetCapability(Found), Found);
    return Error_CreateSuccess();
}

/* Its rows' box inside its padding, less the bar while it shows; the bar along the right; the rows at the offset. */
static Error Behaviour_Layout(void* widget, const UILayoutContext* context)
{
    UIListCapability* Self = GetCapability(widget);
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
    double Content = (double)Self->_count * (double)RowHeight;
    float InnerWidth = Math_MaxFloat(0.0f, Box.x - (2.0f * Padding.x));
    float InnerHeight = Math_MaxFloat(0.0f, Box.y - (2.0f * Padding.y));
    UIScrollMode Mode = Self->Scrollable._modes[UIScrollAxis_Vertical];
    bool IsBarShown = (Mode == UIScrollMode_Always) || ((Mode == UIScrollMode_Auto) && (Content > (double)(InnerHeight + FIT_TOLERANCE)));
    float Bar = IsBarShown ? Thickness : 0.0f;
    Rectangle Rows = { .x = Padding.x, .y = Padding.y, .width = Math_MaxFloat(0.0f, InnerWidth - Bar), .height = InnerHeight };
    float OffsetBefore = Self->Scrollable._offset.y;
    UIScrollableCapability_SetSizes(&Self->Scrollable, (Vector2){ .x = Rows.width, .y = (float)Content }, (Vector2){ .x = Rows.width, .y = Rows.height });
    Self->Scrollable._wheelStep = RowHeight * UI_LIST_WHEEL_ROWS;
    if (Self->_isRevealPending && (Rows.height > 0.0f))
    {
        if (Self->_hasCurrent)
        {
            RevealRow(Self, Self->_current);
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
        // The view moved in the layout (clamped, or a row revealed): its rows are bound again here, so none is missing this frame.
        uint64_t ListId = Widget_GetId(widget);
        Result = UIRowRecycler_Sync(&Self->_rows, Screen, Self->_count, RowHeight, Self->Scrollable._offset.y, Self->Scrollable._viewSize.y,
            &RowHooks, widget);
        Widget* Found = NULL;
        if ((Result.Code != ErrorCode_Success) || !UIScreen_TryGetWidgetById(Screen, ListId, &Found))
        {
            return Result;
        }
        Self = GetCapability(Found);
        PushRowViews(Self, Found);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIRowRecycler_Place(&Self->_rows, Screen, RowHeight, Self->Scrollable._offset.y);
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

/* A left press on a row: the selection by the modifiers (one already selected among several waits for the release), the row current. */
static Error PressRow(Widget* widget, UIListCapability* self, size_t row, uint8_t modifiers)
{
    bool IsControl = (modifiers & GAME_KEY_MODIFIER_CONTROL) != 0U;
    bool IsShift = (modifiers & GAME_KEY_MODIFIER_SHIFT) != 0U;
    Error Result = Error_CreateSuccess();
    bool HasChanged = false;
    switch (self->_mode)
    {
        case UISelectionMode_None:
            break;
        case UISelectionMode_Single:
            if (IsControl && UIListRanges_Contains(&self->_selection, row))
            {
                HasChanged = ClearAll(self);
            }
            else
            {
                HasChanged = SelectOnly(self, row, &Result);
            }
            break;
        case UISelectionMode_Multiple:
            if (IsShift)
            {
                size_t From = self->_hasAnchor ? self->_anchor : (self->_hasCurrent ? self->_current : row);
                HasChanged = SelectSpan(self, From, row, IsControl, &Result);
            }
            else if (IsControl)
            {
                bool IsSelected = UIListRanges_Contains(&self->_selection, row);
                HasChanged = true;
                if (!UIListRanges_Set(&self->_selection, row, 1U, !IsSelected, &self->_selectedCount))
                {
                    Result = SelectionGrowError();
                }
            }
            else if (UIListRanges_Contains(&self->_selection, row) && (self->_selectedCount > 1U))
            {
                self->_isSelectOnRelease = true; // It may be the start of a drag of them all.
            }
            else
            {
                HasChanged = SelectOnly(self, row, &Result);
            }
            break;
    }
    if (!IsShift)
    {
        self->_hasAnchor = true;
        self->_anchor = row;
    }
    SetCurrent(self, row);
    if ((Result.Code != ErrorCode_Success) || !HasChanged)
    {
        return Result;
    }
    return PostSelectionChange(widget, self);
}

static Error OnButtonPress(Widget* widget, UIListCapability* self, const WidgetMouseInputArgs* args)
{
    size_t Row = 0U;
    Vector2 InRow = { .x = 0.0f, .y = 0.0f };
    bool HasRow = TryGetRowAt(self, widget, args->WidgetPosition, &Row, &InRow);
    self->_hasPress = true;
    self->_pressButton = args->Button;
    self->_hasPressRow = HasRow;
    self->_pressRow = Row;
    self->_pressCell = -1;
    self->_isSelectOnRelease = false;
    if (args->Button == GameKey_MouseRight)
    {
        self->_hasContextRow = HasRow;
        self->_contextRow = Row;
        if (!HasRow || UIListRanges_Contains(&self->_selection, Row) || (self->_mode == UISelectionMode_None))
        {
            if (HasRow)
            {
                SetCurrent(self, Row);
            }
            return Error_CreateSuccess();
        }
        Error Result = Error_CreateSuccess();
        bool HasChanged = SelectOnly(self, Row, &Result);
        SetCurrent(self, Row);
        self->_hasAnchor = true;
        self->_anchor = Row;
        return ((Result.Code == ErrorCode_Success) && HasChanged) ? PostSelectionChange(widget, self) : Result;
    }
    if (args->Button != GameKey_MouseLeft)
    {
        return Error_CreateSuccess();
    }
    if (!HasRow)
    {
        // A click on no row clears the selection (Ctrl and Shift keep it).
        bool IsKept = (args->Modifiers & (GAME_KEY_MODIFIER_CONTROL | GAME_KEY_MODIFIER_SHIFT)) != 0U;
        return (!IsKept && ClearAll(self)) ? PostSelectionChange(widget, self) : Error_CreateSuccess();
    }
    UIListRowGeometry Geometry = GetRowGeometry(self);
    self->_pressCell = UIListRow_FindCell(&Geometry, self->Scrollable._viewSize.x, self->_metrics.RowHeight, self->_cellCount, InRow);
    if (self->_pressCell >= 0)
    {
        return Error_CreateSuccess(); // A cell's click toggles it at the release, and selects nothing.
    }
    if (args->ClickCount == 2U)
    {
        SetCurrent(self, Row);
        Error Result = PostItemEvent(widget, UIElementEvent_DoubleClicked, Row);
        return (Result.Code == ErrorCode_Success) ? PostItemEvent(widget, UIElementEvent_Activated, Row) : Result;
    }
    return PressRow(widget, self, Row, args->Modifiers);
}

static Error OnButtonRelease(Widget* widget, UIListCapability* self, const WidgetMouseInputArgs* args)
{
    if (!self->_hasPress || (args->Button != self->_pressButton))
    {
        return Error_CreateSuccess();
    }
    size_t Row = 0U;
    Vector2 InRow = { .x = 0.0f, .y = 0.0f };
    bool IsOverRow = TryGetRowAt(self, widget, args->WidgetPosition, &Row, &InRow);
    bool IsSameRow = IsOverRow && self->_hasPressRow && (Row == self->_pressRow);
    int32_t PressCell = self->_pressCell;
    bool IsSelectOnRelease = self->_isSelectOnRelease;
    bool HasContextRow = self->_hasContextRow;
    self->_hasPress = false;
    self->_hasPressRow = false;
    self->_pressCell = -1;
    self->_isSelectOnRelease = false;
    self->_hasContextRow = false;
    if (args->Button == GameKey_MouseRight)
    {
        return (HasContextRow && IsSameRow) ? PostItemEvent(widget, UIElementEvent_ContextRequested, Row) : Error_CreateSuccess();
    }
    if (!IsSameRow)
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
        UIPropertyValue Value = UIPropertyValue_FromUInt64((uint64_t)Row);
        Error Result = UIScreen_PostWidgetIndexedEvent(Widget_GetScreen(widget), Widget_GetId(widget), UIElementEvent_CellToggled, &Value, Cell);
        return (Result.Code == ErrorCode_Success) ? UIElement_PostCue(widget, UIElementCue_Activate) : Result;
    }
    if (!IsSelectOnRelease)
    {
        return Error_CreateSuccess();
    }
    Error Result = Error_CreateSuccess();
    bool HasChanged = SelectOnly(self, Row, &Result);
    return ((Result.Code == ErrorCode_Success) && HasChanged) ? PostSelectionChange(widget, self) : Result;
}

static Error Behaviour_OnMouseInput(void* widget, const WidgetMouseInputArgs* args)
{
    UIListCapability* Self = GetCapability(widget);
    Error Result = UIElement_PostMouseCues(widget, args, Self->_isEnabled);
    if ((Result.Code != ErrorCode_Success) || !Self->_isEnabled)
    {
        return Result; // Disabled, it swallows the press.
    }
    UIScreen* Screen = Widget_GetScreen(widget);
    UIEventMouse Mouse = MakeMouse(widget, args);
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
    UIListCapability* Self = GetCapability(widget);
    if (Self->_hasPress && (Self->_pressButton == button))
    {
        Self->_hasPress = false;
        Self->_hasPressRow = false;
        Self->_pressCell = -1;
        Self->_isSelectOnRelease = false;
        Self->_hasContextRow = false;
    }
    return Error_CreateSuccess();
}

/* The wheel scrolls it while it can that way, and goes on up the tree at the end. */
static Error Behaviour_OnScrollInput(void* widget, const WidgetScrollInputArgs* args, bool* outIsHandled)
{
    UIListCapability* Self = GetCapability(widget);
    float Delta = -args->ScrollDelta.y * Self->Scrollable._wheelStep;
    if ((Delta == 0.0f) || !UIScrollableCapability_CanScroll(&Self->Scrollable, UIScrollAxis_Vertical, Delta))
    {
        return Error_CreateSuccess();
    }
    *outIsHandled = true;
    UIScrollableCapability_ScrollBy(&Self->Scrollable, (Vector2){ .x = 0.0f, .y = Delta });
    return Error_CreateSuccess();
}

/* Moves the current row by the keys: alone the row reached is selected (and the anchor), with Shift the run from the anchor, with Ctrl
 * only the current row moves. A whole change when the selection changed. */
static Error MoveCurrent(Widget* widget, UIListCapability* self, int64_t target, uint8_t modifiers)
{
    if (self->_count == 0U)
    {
        return Error_CreateSuccess();
    }
    int64_t Last = (int64_t)self->_count - 1;
    size_t Row = (size_t)((target < 0) ? 0 : ((target > Last) ? Last : target));
    bool IsControl = (modifiers & GAME_KEY_MODIFIER_CONTROL) != 0U;
    bool IsShift = (modifiers & GAME_KEY_MODIFIER_SHIFT) != 0U;
    Error Result = Error_CreateSuccess();
    bool HasChanged = false;
    if ((self->_mode == UISelectionMode_Multiple) && IsShift)
    {
        size_t From = self->_hasAnchor ? self->_anchor : (self->_hasCurrent ? self->_current : Row);
        self->_hasAnchor = true;
        self->_anchor = From;
        HasChanged = SelectSpan(self, From, Row, IsControl, &Result);
    }
    else if (!((self->_mode == UISelectionMode_Multiple) && IsControl) && (self->_mode != UISelectionMode_None))
    {
        HasChanged = SelectOnly(self, Row, &Result);
        self->_hasAnchor = true;
        self->_anchor = Row;
    }
    SetCurrent(self, Row);
    RevealRow(self, Row);
    if ((Result.Code != ErrorCode_Success) || !HasChanged)
    {
        return Result;
    }
    Result = PostSelectionChange(widget, self);
    return (Result.Code == ErrorCode_Success) ? UIElement_PostCue(widget, UIElementCue_ValueStep) : Result;
}

/* The context menu's key: ContextRequested for the current row, and the menu asked for at its middle. */
static Error OnContextKey(Widget* widget, UIListCapability* self, bool* outIsHandled)
{
    *outIsHandled = true;
    UIScreen* Screen = Widget_GetScreen(widget);
    Vector2 Middle = UIScreen_WidgetToScreen(Screen, widget, (Vector2){ .x = 0.5f, .y = 0.5f });
    if (self->_hasCurrent)
    {
        Error Result = PostItemEvent(widget, UIElementEvent_ContextRequested, self->_current);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        Vector2 Box = GetBoxSize(widget);
        float RowMiddle = self->_metrics.Padding.y + (float)(((double)self->_current + 0.5) * (double)self->_metrics.RowHeight)
            - self->Scrollable._offset.y;
        if ((Box.y > 0.0f) && (RowMiddle >= 0.0f) && (RowMiddle <= Box.y))
        {
            Middle = UIScreen_WidgetToScreen(Screen, widget, (Vector2){ .x = 0.5f, .y = RowMiddle / Box.y });
        }
    }
    return UIContextMenu_Request(Screen, Widget_GetId(widget), Middle, true, NULL);
}

static Error Behaviour_OnKeyboardInput(void* widget, const WidgetKeyboardInputArgs* args, bool* outIsHandled)
{
    UIListCapability* Self = GetCapability(widget);
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
    int64_t Current = Self->_hasCurrent ? (int64_t)Self->_current : -1;
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
            return MoveCurrent(widget, Self, (int64_t)Self->_count - 1, args->Modifiers);
        default:
            break;
    }
    if (!IsPress)
    {
        return Error_CreateSuccess();
    }
    if ((args->Key == GameKey_Space) && Self->_hasCurrent)
    {
        *outIsHandled = true;
        Error Result = Error_CreateSuccess();
        bool HasChanged = false;
        if ((Self->_mode == UISelectionMode_Multiple) && ((args->Modifiers & GAME_KEY_MODIFIER_CONTROL) != 0U))
        {
            bool IsSelected = UIListRanges_Contains(&Self->_selection, Self->_current);
            HasChanged = true;
            if (!UIListRanges_Set(&Self->_selection, Self->_current, 1U, !IsSelected, &Self->_selectedCount))
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
        if ((Self->_count == 0U) || (Self->_selectedCount == Self->_count))
        {
            return Error_CreateSuccess();
        }
        if (!UIListRanges_Set(&Self->_selection, 0U, Self->_count, true, &Self->_selectedCount))
        {
            return SelectionGrowError();
        }
        return PostSelectionChange(widget, Self);
    }
    if (UIElement_IsActionKey(Screen, GameAction_UIAccept, args->Key) && Self->_hasCurrent)
    {
        *outIsHandled = true;
        Error Result = PostItemEvent(widget, UIElementEvent_Activated, Self->_current);
        return (Result.Code == ErrorCode_Success) ? UIElement_PostCue(widget, UIElementCue_Activate) : Result;
    }
    return Error_CreateSuccess();
}

/* Entered by a key with no current row: the first selected row, or the first row, becomes it, so the keys have somewhere to start. */
static Error Behaviour_OnEnter(void* widget, UIEnterCause cause)
{
    UIListCapability* Self = GetCapability(widget);
    if ((cause != UIEnterCause_Press) && !Self->_hasCurrent && (Self->_count > 0U))
    {
        size_t Row = (Self->_selection._count > 0U) ? PeekRangeAt(&Self->_selection, 0U)->Start : 0U;
        SetCurrent(Self, Row);
        Self->_isRevealPending = true;
    }
    return Error_CreateSuccess();
}


// Fields.
static const UIKindCapability ListCapabilities[] =
{
    { .Capability = UIElementCapability_List, .Resolve = ResolveListCapability },
    { .Capability = UIElementCapability_Scrollable, .Resolve = ResolveScrollable }
};

static const UIKindBehaviour ListBehaviour =
{
    .Kind = UIElementKind_List,
    .Capabilities = ListCapabilities,
    .CapabilityCount = sizeof(ListCapabilities) / sizeof(ListCapabilities[0]),
    .BaseSize = sizeof(UIListLookBase),
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
    UIListCapability* Self = NULL;
    Error Result = ResolveList(screen, sourceId, &Target, &Self);
    if ((Result.Code != ErrorCode_Success) || !Self->_isDragSource || !Self->_isEnabled)
    {
        Error_Deconstruct(&Result);
        return Error_CreateSuccess();
    }
    size_t Row = 0U;
    Vector2 InRow = { .x = 0.0f, .y = 0.0f };
    if (!TryGetRowAt(Self, Target, UIScreen_ScreenToWidget(screen, Target, pressScreenPosition), &Row, &InRow)
        || !UIListRanges_Contains(&Self->_selection, Row))
    {
        return Error_CreateSuccess(); // Only selected rows go: a press on no row, or in a list that selects none, drags nothing.
    }
    UIListRowGeometry Geometry = GetRowGeometry(Self);
    if (UIListRow_FindCell(&Geometry, Self->Scrollable._viewSize.x, Self->_metrics.RowHeight, Self->_cellCount, InRow) >= 0)
    {
        return Error_CreateSuccess(); // A press on a toggle cell toggles it at its release, and drags nothing.
    }
    outPayload->Kind = Self->_dragKind;
    outPayload->Value = (uint64_t)Row;
    if (Self->_selectedCount > 1U)
    {
        Result = UIDragPayload_SetTextCount(outPayload, Self->_selectedCount, (const unsigned char*)u8"rows");
    }
    else
    {
        // The row's own text, as it shows.
        UIListRowData Data = { .Text = &Self->_scratch, .Icon = UIElementIcon_None(), .IsDimmed = false };
        GenericBuffer_Clear(&Self->_scratch);
        Result = (Self->_rowProvider != NULL) ? Self->_rowProvider(screen, sourceId, &Self->_providerData, Row, &Data) : Error_CreateSuccess();
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
const UIKindBehaviour* UIList_GetKindBehaviour(void)
{
    return &ListBehaviour;
}


// Public functions: helpers.
UIListOptions UIListOptions_Create(void)
{
    UIListOptions Options =
    {
        .Element = UIElementOptions_Create(),
        .SelectionMode = UISelectionMode_Multiple,
        .CountProvider = NULL,
        .RowProvider = NULL,
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

bool UISelectionMode_IsValid(UISelectionMode mode)
{
    switch (mode)
    {
        case UISelectionMode_None:
        case UISelectionMode_Single:
        case UISelectionMode_Multiple:
            return true;
    }
    return false;
}


// Public functions: calling code.
Error UIList_SetProvider(UIScreen* screen, uint64_t widgetId, UIListCountProvider countProvider, UIListRowProvider rowProvider,
    const UserData* userData)
{
    if ((countProvider == NULL) != (rowProvider == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIList_SetProvider: both providers or neither.");
    }
    Widget* Target = NULL;
    UIListCapability* Self = NULL;
    Error Result = ResolveList(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Self->_countProvider = countProvider;
    Self->_rowProvider = rowProvider;
    Self->_providerData = (userData != NULL) ? *userData : UserData_CreateEmpty();
    ClearAll(Self);
    Self->_hasCurrent = false;
    Self->_hasAnchor = false;
    Self->_hasHovered = false;
    Self->_hasPressRow = false;
    Self->_hasContextRow = false;
    Self->_isSelectOnRelease = false;
    UIRowRecycler_Invalidate(&Self->_rows);
    size_t Count = (countProvider != NULL) ? countProvider(screen, widgetId, &Self->_providerData) : 0U;
    if (UIScreen_TryGetWidgetById(screen, widgetId, &Target))
    {
        GetCapability(Target)->_count = Count;
    }
    return Error_CreateSuccess();
}

Error UIList_Refresh(UIScreen* screen, uint64_t widgetId)
{
    Widget* Target = NULL;
    UIListCapability* Self = NULL;
    Error Result = ResolveList(screen, widgetId, &Target, &Self);
    if (Result.Code == ErrorCode_Success)
    {
        UIRowRecycler_Invalidate(&Self->_rows);
    }
    return Result;
}

Error UIList_InsertRows(UIScreen* screen, uint64_t widgetId, size_t index, size_t count)
{
    Widget* Target = NULL;
    UIListCapability* Self = NULL;
    Error Result = ResolveList(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((index > Self->_count) || (count > (SIZE_MAX - Self->_count)))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIList_InsertRows: the rows must stand within the count.");
    }
    if (count == 0U)
    {
        return Error_CreateSuccess();
    }
    if (!UIListRanges_Insert(&Self->_selection, index, count))
    {
        return SelectionGrowError();
    }
    ShiftForInsert(Self->_hasCurrent, &Self->_current, index, count);
    ShiftForInsert(Self->_hasAnchor, &Self->_anchor, index, count);
    ShiftForInsert(Self->_hasHovered, &Self->_hovered, index, count);
    ShiftForInsert(Self->_hasPressRow, &Self->_pressRow, index, count);
    ShiftForInsert(Self->_hasContextRow, &Self->_contextRow, index, count);
    Self->_count += count;
    UIRowRecycler_Invalidate(&Self->_rows);
    return Error_CreateSuccess();
}

Error UIList_RemoveRows(UIScreen* screen, uint64_t widgetId, size_t index, size_t count)
{
    Widget* Target = NULL;
    UIListCapability* Self = NULL;
    Error Result = ResolveList(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((index > Self->_count) || (count > (Self->_count - index)))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIList_RemoveRows: the rows must lie within the count.");
    }
    if (count == 0U)
    {
        return Error_CreateSuccess();
    }
    UIListRanges_Remove(&Self->_selection, index, count, &Self->_selectedCount);
    size_t NewCount = Self->_count - count;
    if (Self->_hasCurrent && !ShiftForRemove(&Self->_current, index, count))
    {
        // The current row went: the row that took its place, or the last.
        Self->_hasCurrent = NewCount > 0U;
        Self->_current = (index < NewCount) ? index : ((NewCount > 0U) ? (NewCount - 1U) : 0U);
    }
    Self->_hasAnchor = Self->_hasAnchor && ShiftForRemove(&Self->_anchor, index, count);
    Self->_hasHovered = Self->_hasHovered && ShiftForRemove(&Self->_hovered, index, count);
    Self->_hasPressRow = Self->_hasPressRow && ShiftForRemove(&Self->_pressRow, index, count);
    Self->_hasContextRow = Self->_hasContextRow && ShiftForRemove(&Self->_contextRow, index, count);
    Self->_count = NewCount;
    UIRowRecycler_Invalidate(&Self->_rows);
    return Error_CreateSuccess();
}

Error UIList_SetSelected(UIScreen* screen, uint64_t widgetId, size_t index, bool isSelected)
{
    return UIList_SetRangeSelected(screen, widgetId, index, 1U, isSelected);
}

Error UIList_SetRangeSelected(UIScreen* screen, uint64_t widgetId, size_t first, size_t count, bool isSelected)
{
    Widget* Target = NULL;
    UIListCapability* Self = NULL;
    Error Result = ResolveList(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((first > Self->_count) || (count > (Self->_count - first)))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIList_SetRangeSelected: the rows must lie within the count.");
    }
    if (count == 0U)
    {
        return Error_CreateSuccess();
    }
    if (isSelected && ((Self->_mode == UISelectionMode_None) || ((Self->_mode == UISelectionMode_Single) && (count > 1U))))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIList_SetRangeSelected: the list's mode does not let that many rows be selected.");
    }
    if (isSelected && (Self->_mode == UISelectionMode_Single))
    {
        ClearAll(Self);
    }
    return UIListRanges_Set(&Self->_selection, first, count, isSelected, &Self->_selectedCount) ? Error_CreateSuccess() : SelectionGrowError();
}

Error UIList_ClearSelection(UIScreen* screen, uint64_t widgetId)
{
    Widget* Target = NULL;
    UIListCapability* Self = NULL;
    Error Result = ResolveList(screen, widgetId, &Target, &Self);
    if (Result.Code == ErrorCode_Success)
    {
        ClearAll(Self);
    }
    return Result;
}

Error UIList_IsSelected(UIScreen* screen, uint64_t widgetId, size_t index, bool* outIsSelected)
{
    Widget* Target = NULL;
    UIListCapability* Self = NULL;
    Error Result = ResolveList(screen, widgetId, &Target, &Self);
    if (Result.Code == ErrorCode_Success)
    {
        *outIsSelected = UIListRanges_Contains(&Self->_selection, index);
    }
    return Result;
}

Error UIList_GetSelection(UIScreen* screen, uint64_t widgetId, size_t* outSelectedCount, size_t* outRangeCount)
{
    Widget* Target = NULL;
    UIListCapability* Self = NULL;
    Error Result = ResolveList(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (outSelectedCount != NULL)
    {
        *outSelectedCount = Self->_selectedCount;
    }
    if (outRangeCount != NULL)
    {
        *outRangeCount = Self->_selection._count;
    }
    return Error_CreateSuccess();
}

Error UIList_GetSelectionRange(UIScreen* screen, uint64_t widgetId, size_t rangeIndex, UIListRange* outRange)
{
    Widget* Target = NULL;
    UIListCapability* Self = NULL;
    Error Result = ResolveList(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (rangeIndex >= Self->_selection._count)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIList_GetSelectionRange: no such run.");
    }
    *outRange = *PeekRangeAt(&Self->_selection, rangeIndex);
    return Error_CreateSuccess();
}

Error UIList_SetCurrent(UIScreen* screen, uint64_t widgetId, size_t index)
{
    Widget* Target = NULL;
    UIListCapability* Self = NULL;
    Error Result = ResolveList(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (index >= Self->_count)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIList_SetCurrent: the row must lie within the count.");
    }
    SetCurrent(Self, index);
    Self->_hasAnchor = true;
    Self->_anchor = index;
    Self->_isRevealPending = true;
    return Error_CreateSuccess();
}

Error UIList_GetCurrent(UIScreen* screen, uint64_t widgetId, bool* outHasCurrent, size_t* outIndex)
{
    Widget* Target = NULL;
    UIListCapability* Self = NULL;
    Error Result = ResolveList(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    *outHasCurrent = Self->_hasCurrent;
    if (outIndex != NULL)
    {
        *outIndex = Self->_current;
    }
    return Error_CreateSuccess();
}

Error UIList_Reveal(UIScreen* screen, uint64_t widgetId, size_t index)
{
    Widget* Target = NULL;
    UIListCapability* Self = NULL;
    Error Result = ResolveList(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (index >= Self->_count)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIList_Reveal: the row must lie within the count.");
    }
    RevealRow(Self, index);
    return Error_CreateSuccess();
}

Error UIList_GetItemAt(UIScreen* screen, uint64_t widgetId, Vector2 screenPosition, bool* outHasRow, size_t* outIndex)
{
    Widget* Target = NULL;
    UIListCapability* Self = NULL;
    Error Result = ResolveList(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    size_t Row = 0U;
    *outHasRow = TryGetRowAt(Self, Target, UIScreen_ScreenToWidget(screen, Target, screenPosition), &Row, NULL);
    if (outIndex != NULL)
    {
        *outIndex = Row;
    }
    return Error_CreateSuccess();
}

Error UIList_GetDropIndex(UIScreen* screen, uint64_t widgetId, Vector2 screenPosition, size_t* outIndex)
{
    Widget* Target = NULL;
    UIListCapability* Self = NULL;
    Error Result = ResolveList(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Vector2 Content = ToContent(Self, Target, UIScreen_ScreenToWidget(screen, Target, screenPosition));
    double Place = Math_FloorDouble(((double)Content.y / (double)Self->_metrics.RowHeight) + 0.5);
    *outIndex = (Place <= 0.0) ? 0U : ((Place >= (double)Self->_count) ? Self->_count : (size_t)Place);
    return Error_CreateSuccess();
}

Error UIList_SetSelectionMode(UIScreen* screen, uint64_t widgetId, UISelectionMode mode)
{
    if (!UISelectionMode_IsValid(mode))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIList_SetSelectionMode: an unknown mode.");
    }
    Widget* Target = NULL;
    UIListCapability* Self = NULL;
    Error Result = ResolveList(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Self->_mode = mode;
    if ((mode == UISelectionMode_None) || ((mode == UISelectionMode_Single) && (Self->_selectedCount > 1U)))
    {
        bool HasFirst = Self->_selection._count > 0U;
        size_t First = HasFirst ? PeekRangeAt(&Self->_selection, 0U)->Start : 0U;
        ClearAll(Self);
        if (HasFirst && (mode == UISelectionMode_Single))
        {
            UIListRanges_Set(&Self->_selection, First, 1U, true, &Self->_selectedCount); // Room for one run is always there.
        }
    }
    return Error_CreateSuccess();
}

Error UIList_SetCells(UIScreen* screen, uint64_t widgetId, size_t count, const UIElementIcon* icons)
{
    if (count > UI_LIST_MAX_CELLS)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIList_SetCells: at most UI_LIST_MAX_CELLS cells.");
    }
    for (size_t Index = 0U; (icons != NULL) && (Index < count); Index++)
    {
        if (!UIElementIcon_IsValid(&icons[Index]))
        {
            return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIList_SetCells: a cell's icon is not valid.");
        }
    }
    Widget* Target = NULL;
    UIListCapability* Self = NULL;
    Error Result = ResolveList(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Self->_cellCount = count;
    for (size_t Index = 0U; Index < UI_LIST_MAX_CELLS; Index++)
    {
        Self->_cellIcons[Index] = ((icons != NULL) && (Index < count)) ? icons[Index] : UIElementIcon_None();
    }
    UIRowRecycler_Invalidate(&Self->_rows);
    return Error_CreateSuccess();
}

Error UIList_SetDragSource(UIScreen* screen, uint64_t widgetId, bool isDragSource, uint32_t dragKind)
{
    Widget* Target = NULL;
    UIListCapability* Self = NULL;
    Error Result = ResolveList(screen, widgetId, &Target, &Self);
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

Error UIList_SetIsEnabled(UIScreen* screen, uint64_t widgetId, bool isEnabled)
{
    Widget* Target = NULL;
    UIListCapability* Self = NULL;
    Error Result = ResolveList(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Self->_isEnabled = isEnabled;
    if (!isEnabled)
    {
        Self->_hasPress = false;
        Self->_hasPressRow = false;
        Self->_pressCell = -1;
        Self->_isSelectOnRelease = false;
        Self->_hasContextRow = false;
    }
    return Widget_SetIsFocusable(Target, isEnabled);
}


// Public functions: behaviour helpers.
void UIListCapability_SetMetrics(UIListCapability* self, Widget* widget, const UIListMetrics* metrics)
{
    UIListMetrics Clean =
    {
        .Padding = { .x = CleanLength(metrics->Padding.x), .y = CleanLength(metrics->Padding.y) },
        .RowHeight = Math_MaxFloat(1.0f, CleanLength(metrics->RowHeight)),
        .RowPadding = CleanLength(metrics->RowPadding),
        .CellSize = CleanLength(metrics->CellSize),
        .InsertionThickness = CleanLength(metrics->InsertionThickness)
    };
    bool IsSame = (Clean.Padding.x == self->_metrics.Padding.x) && (Clean.Padding.y == self->_metrics.Padding.y)
        && (Clean.RowHeight == self->_metrics.RowHeight) && (Clean.RowPadding == self->_metrics.RowPadding)
        && (Clean.CellSize == self->_metrics.CellSize) && (Clean.InsertionThickness == self->_metrics.InsertionThickness);
    if (IsSame)
    {
        return;
    }
    self->_metrics = Clean;
    Widget_InvalidateMeasure(widget);
}

UIElementState UIListCapability_GetState(const UIListCapability* self, const Widget* widget)
{
    return UIElementState_Choose(self->_isEnabled, false, false, Widget_IsEntered(widget), false, Widget_IsHovered(widget),
        Widget_IsFocused(widget));
}

void UIListCapability_Measure(const UIListCapability* self, Vector2* outSize)
{
    const UIListMetrics* Metrics = &self->_metrics;
    float Cells = (float)self->_cellCount * (Metrics->CellSize + Metrics->RowPadding);
    outSize->x = (2.0f * Metrics->Padding.x) + (2.0f * Metrics->RowPadding) + LIST_MIN_TEXT_WIDTH + Cells + self->_barThickness;
    outSize->y = (2.0f * Metrics->Padding.y) + ((float)self->_visibleRows * Metrics->RowHeight);
}

Error UIList_RegisterTypes(UIKit* kit, UIWidgetFactory* factory)
{
    return UIListRow_RegisterType(kit, factory, &kit->_listRowType);
}

Error UIList_SetParts(UIScreen* screen, uint64_t widgetId, uint64_t rowsBoxId, uint64_t barId)
{
    Widget* Target = NULL;
    UIListCapability* Self = NULL;
    Error Result = ResolveList(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Self->_rowsBoxId = rowsBoxId;
    Self->_barId = barId;
    size_t Count = (Self->_countProvider != NULL) ? Self->_countProvider(screen, widgetId, &Self->_providerData) : 0U;
    if (UIScreen_TryGetWidgetById(screen, widgetId, &Target))
    {
        GetCapability(Target)->_count = Count; // The provider ran: the list is found again.
    }
    return Error_CreateSuccess();
}


// Public functions: the selection's arithmetic.
bool UIListRanges_Contains(const GenericBuffer* ranges, size_t index)
{
    size_t At = FindFirstEndingAfter(ranges, index);
    return (At < ranges->_count) && (PeekRangeAt(ranges, At)->Start <= index);
}

bool UIListRanges_Set(GenericBuffer* ranges, size_t start, size_t count, bool isSelected, size_t* outCount)
{
    if (count == 0U)
    {
        return true;
    }
    size_t End = (count > (SIZE_MAX - start)) ? SIZE_MAX : (start + count);
    // A run cut in two needs one more slot, and so does a new run: room first, so a failure changes nothing.
    if (!GenericBuffer_EnsureTotalCapacity(ranges, ranges->_count + 1U))
    {
        return false;
    }
    if (isSelected)
    {
        // Every run touching [start, End) (overlapping it, or ending where it starts, or starting where it ends) merges into one.
        size_t First = (start > 0U) ? FindFirstEndingAfter(ranges, start - 1U) : 0U;
        size_t Last = First;
        size_t NewStart = start;
        size_t NewEnd = End;
        size_t Removed = 0U;
        while ((Last < ranges->_count) && (PeekRangeAt(ranges, Last)->Start <= End))
        {
            const UIListRange* Range = PeekRangeAt(ranges, Last);
            NewStart = Math_MinSizeT(NewStart, Range->Start);
            NewEnd = Math_MaxSizeT(NewEnd, GetRangeEnd(Range));
            Removed += Range->Count;
            Last++;
        }
        UIListRange Merged = { .Start = NewStart, .Count = NewEnd - NewStart };
        if (Last > First)
        {
            *GetRangeAt(ranges, First) = Merged;
            GenericBuffer_RemoveRange(ranges, First + 1U, Last);
        }
        else
        {
            GenericBuffer_Insert(ranges, &Merged, First); // Cannot fail: the room is there.
        }
        *outCount = *outCount - Removed + Merged.Count;
        return true;
    }
    // Deselecting: each run overlapping [start, End) is cut; one run holding it inside splits.
    size_t At = FindFirstEndingAfter(ranges, start);
    while ((At < ranges->_count) && (PeekRangeAt(ranges, At)->Start < End))
    {
        UIListRange* Range = GetRangeAt(ranges, At);
        size_t RangeEnd = GetRangeEnd(Range);
        size_t CutStart = Math_MaxSizeT(Range->Start, start);
        size_t CutEnd = Math_MinSizeT(RangeEnd, End);
        *outCount -= CutEnd - CutStart;
        bool KeepsHead = Range->Start < CutStart;
        bool KeepsTail = RangeEnd > CutEnd;
        if (KeepsHead && KeepsTail)
        {
            UIListRange Tail = { .Start = CutEnd, .Count = RangeEnd - CutEnd };
            Range->Count = CutStart - Range->Start;
            GenericBuffer_Insert(ranges, &Tail, At + 1U); // Cannot fail: the room is there, and this happens once.
            return true;
        }
        if (KeepsHead)
        {
            Range->Count = CutStart - Range->Start;
            At++;
        }
        else if (KeepsTail)
        {
            Range->Start = CutEnd;
            Range->Count = RangeEnd - CutEnd;
            At++;
        }
        else
        {
            GenericBuffer_RemoveAt(ranges, At);
        }
    }
    return true;
}

bool UIListRanges_Insert(GenericBuffer* ranges, size_t index, size_t count)
{
    if (count == 0U)
    {
        return true;
    }
    if (!GenericBuffer_EnsureTotalCapacity(ranges, ranges->_count + 1U))
    {
        return false;
    }
    size_t At = FindFirstEndingAfter(ranges, index);
    if ((At < ranges->_count) && (PeekRangeAt(ranges, At)->Start < index))
    {
        // The new rows fall inside this run: it splits, the new rows unselected between its two parts.
        UIListRange* Range = GetRangeAt(ranges, At);
        UIListRange Tail = { .Start = index + count, .Count = GetRangeEnd(Range) - index };
        Range->Count = index - Range->Start;
        GenericBuffer_Insert(ranges, &Tail, At + 1U); // Cannot fail: the room is there.
        At += 2U;
    }
    for (; At < ranges->_count; At++)
    {
        GetRangeAt(ranges, At)->Start += count;
    }
    return true;
}

void UIListRanges_Remove(GenericBuffer* ranges, size_t index, size_t count, size_t* outCount)
{
    if (count == 0U)
    {
        return;
    }
    size_t End = (count > (SIZE_MAX - index)) ? SIZE_MAX : (index + count);
    size_t Gone = End - index;
    size_t At = FindFirstEndingAfter(ranges, index);
    size_t MergeAt = (At > 0U) ? (At - 1U) : 0U;
    while (At < ranges->_count)
    {
        UIListRange* Range = GetRangeAt(ranges, At);
        size_t RangeEnd = GetRangeEnd(Range);
        if (Range->Start >= End)
        {
            Range->Start -= Gone;
            At++;
            continue;
        }
        // It overlaps the removed rows: what lies inside them goes, what lies past them moves up to meet what lies before.
        size_t Before = (Range->Start < index) ? (index - Range->Start) : 0U;
        size_t After = (RangeEnd > End) ? (RangeEnd - End) : 0U;
        *outCount -= Range->Count - Before - After;
        if ((Before + After) == 0U)
        {
            GenericBuffer_RemoveAt(ranges, At);
            continue;
        }
        Range->Start = (Range->Start < index) ? Range->Start : index;
        Range->Count = Before + After;
        At++;
    }
    MergeForward(ranges, MergeAt);
    if ((MergeAt + 1U) < ranges->_count)
    {
        MergeForward(ranges, MergeAt + 1U);
    }
}
