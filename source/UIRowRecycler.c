#include "UIRowRecycler.h"
#include "UIPlacement.h"
#include "UIScreen.h"
#include "UIWidget.h"
#include "wr/WRMath.h"


// Static functions.
static UIRecycledRow* GetRowAt(UIRowRecycler* self, size_t index)
{
    return GenericBuffer_GetPointerToElement(&self->_rows, index);
}

/* Forgets the row widgets that were destroyed. Returns whether any was. */
static bool ForgetDeadRows(UIRowRecycler* self, const UIScreen* screen)
{
    bool HasForgotten = false;
    for (size_t Index = self->_rows._count; Index-- > 0U;)
    {
        if (!UIScreen_IsWidgetAlive(screen, GetRowAt(self, Index)->WidgetId))
        {
            GenericBuffer_RemoveAt(&self->_rows, Index);
            HasForgotten = true;
        }
    }
    return HasForgotten;
}

static bool IsInRange(size_t index, size_t first, size_t count)
{
    return (index >= first) && ((index - first) < count);
}

/* Whether a widget already shows a data row. */
static bool IsShown(const UIRowRecycler* self, size_t dataIndex)
{
    uint64_t Unused = 0U;
    return UIRowRecycler_FindWidget(self, dataIndex, &Unused);
}

/* The next widget free to show a row (bound to none, or to a row out of view) from @p start on, or GENERIC_BUFFER_INDEX_INVALID. */
static size_t FindFree(UIRowRecycler* self, size_t start, size_t first, size_t count)
{
    for (size_t Index = start; Index < self->_rows._count; Index++)
    {
        const UIRecycledRow* Row = GetRowAt(self, Index);
        if (!Row->IsBound || !IsInRange(Row->DataIndex, first, count))
        {
            return Index;
        }
    }
    return GENERIC_BUFFER_INDEX_INVALID;
}


// Public functions.
void UIRowRecycler_Construct(UIRowRecycler* self)
{
    GenericBuffer_AllocateVariable(&self->_rows, 0U, sizeof(UIRecycledRow));
    self->_hasSynced = false;
    self->_syncedFirst = 0U;
    self->_syncedCount = 0U;
}

void UIRowRecycler_Deconstruct(UIRowRecycler* self)
{
    Memory_Free(self->_rows._data);
    Memory_Zero(&self->_rows, sizeof(self->_rows));
}

void UIRowRecycler_GetVisibleRange(size_t rowCount, float rowHeight, float offset, float viewHeight, size_t* outFirst, size_t* outCount)
{
    *outFirst = 0U;
    *outCount = 0U;
    if (!(rowHeight > 0.0f) || !(viewHeight > 0.0f) || (rowCount == 0U) || Math_IsInfinityFloat(rowHeight))
    {
        return;
    }
    double Top = Math_IsNaNFloat(offset) ? 0.0 : Math_MaxDouble((double)offset, 0.0);
    double FirstRow = Math_FloorDouble(Top / (double)rowHeight);
    double EndRow = Math_CeilDouble((Top + (double)viewHeight) / (double)rowHeight);
    if (!(FirstRow < (double)rowCount))
    {
        return;
    }
    size_t First = (size_t)FirstRow;
    size_t End = (EndRow < (double)rowCount) ? (size_t)EndRow : rowCount;
    size_t Count = (End > First) ? (End - First) : 0U;
    *outFirst = First;
    *outCount = (Count < UI_ROW_RECYCLER_MAX_ROWS) ? Count : UI_ROW_RECYCLER_MAX_ROWS;
}

Error UIRowRecycler_Sync(UIRowRecycler* self, UIScreen* screen, size_t rowCount, float rowHeight, float offset, float viewHeight,
    const UIRowRecyclerHooks* hooks, void* owner)
{
    bool HasForgotten = ForgetDeadRows(self, screen);
    size_t First = 0U;
    size_t Count = 0U;
    UIRowRecycler_GetVisibleRange(rowCount, rowHeight, offset, viewHeight, &First, &Count);
    if (!HasForgotten && self->_hasSynced && (self->_syncedFirst == First) && (self->_syncedCount == Count))
    {
        return Error_CreateSuccess();
    }
    self->_hasSynced = false; // Set again only once every row is where it belongs, so a failure tries again next time.

    // Each row in view that no widget shows takes a free widget, or a new one.
    size_t Free = 0U;
    for (size_t DataIndex = First; DataIndex < (First + Count); DataIndex++)
    {
        if (IsShown(self, DataIndex))
        {
            continue;
        }
        Free = FindFree(self, Free, First, Count);
        uint64_t WidgetId = 0U;
        Error Result = Error_CreateSuccess();
        if (Free != GENERIC_BUFFER_INDEX_INVALID)
        {
            WidgetId = GetRowAt(self, Free)->WidgetId;
        }
        else
        {
            if (!GenericBuffer_EnsureTotalCapacity(&self->_rows, self->_rows._count + 1U))
            {
                return Error_Construct2(ErrorCode_BufferTooLarge, "UIRowRecycler: the list of rows could not grow.");
            }
            Result = hooks->CreateRow(owner, &WidgetId);
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }
            UIRecycledRow New = { .WidgetId = WidgetId, .DataIndex = 0U, .IsBound = false };
            GenericBuffer_AddLast(&self->_rows, &New); // Cannot fail: the room is there.
            Free = self->_rows._count - 1U;
        }
        Result = hooks->BindRow(owner, WidgetId, DataIndex);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        // A hook may have destroyed widgets: the row is found again by its widget.
        for (size_t Index = 0U; Index < self->_rows._count; Index++)
        {
            UIRecycledRow* Row = GetRowAt(self, Index);
            if (Row->WidgetId == WidgetId)
            {
                Row->DataIndex = DataIndex;
                Row->IsBound = true;
            }
        }
    }
    // What is left over shows nothing.
    for (size_t Index = 0U; Index < self->_rows._count; Index++)
    {
        UIRecycledRow* Row = GetRowAt(self, Index);
        if (Row->IsBound && !IsInRange(Row->DataIndex, First, Count))
        {
            uint64_t WidgetId = Row->WidgetId;
            Row->IsBound = false;
            Error Result = hooks->UnbindRow(owner, WidgetId);
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }
        }
    }
    self->_hasSynced = true;
    self->_syncedFirst = First;
    self->_syncedCount = Count;
    return Error_CreateSuccess();
}

void UIRowRecycler_Invalidate(UIRowRecycler* self)
{
    self->_hasSynced = false;
    for (size_t Index = 0U; Index < self->_rows._count; Index++)
    {
        GetRowAt(self, Index)->IsBound = false;
    }
}

Error UIRowRecycler_Place(const UIRowRecycler* self, UIScreen* screen, float rowHeight, float offset)
{
    for (size_t Index = 0U; Index < self->_rows._count; Index++)
    {
        const UIRecycledRow* Row = GenericBuffer_GetPointerToElement((GenericBuffer*)&self->_rows, Index);
        Widget* Found = NULL;
        if (!UIScreen_TryGetWidgetById(screen, Row->WidgetId, &Found))
        {
            continue;
        }
        UIPlacement Placement = UIPlacement_FromTopLeft(0.0f, 0.0f, 0.0f, 0.0f);
        if (Row->IsBound)
        {
            // In double: far down a long list a float row times a height would round the row's top.
            float Top = (float)(((double)Row->DataIndex * (double)rowHeight) - (double)offset);
            Placement = (UIPlacement){ .AnchorMin = { .x = 0.0f, .y = 0.0f }, .AnchorMax = { .x = 1.0f, .y = 0.0f },
                .OffsetMin = { .x = 0.0f, .y = Top }, .OffsetMax = { .x = 0.0f, .y = Top + rowHeight } };
        }
        Error Result = Widget_SetPlacement(Found, &Placement);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}

const UIRecycledRow* UIRowRecycler_GetAt(const UIRowRecycler* self, size_t index)
{
    return (index < self->_rows._count) ? GenericBuffer_GetPointerToElement((GenericBuffer*)&self->_rows, index) : NULL;
}

bool UIRowRecycler_FindWidget(const UIRowRecycler* self, size_t dataIndex, uint64_t* outWidgetId)
{
    for (size_t Index = 0U; Index < self->_rows._count; Index++)
    {
        const UIRecycledRow* Row = GenericBuffer_GetPointerToElement((GenericBuffer*)&self->_rows, Index);
        if (Row->IsBound && (Row->DataIndex == dataIndex))
        {
            *outWidgetId = Row->WidgetId;
            return true;
        }
    }
    return false;
}
