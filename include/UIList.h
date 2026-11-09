#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "wr/WRMemory.h"
#include "wr/WRUserData.h"
#include "UIElement.h"
#include "UIRowRecycler.h"
#include "UIScrollable.h"
#include "UIWidget.h"


/**
 * @file UIList.h
 * @brief The list element (kind list): rows of data that live with the caller, only the rows in view made, so a hundred thousand rows
 *        cost what the rows on screen cost.
 *
 * DATA. The rows come through two providers: how many there are, and what one row shows (its text, an icon, whether it is dimmed, and
 * its toggle cells). The list asks only for the rows in view (UIRowRecycler.h: the row widgets are made once and bound again as the
 * view moves), at a fixed row height. It reads its count when it is made (UIKit_CreateList), so code can select and reveal rows at once.
 * Calling code tells it what changed: UIList_Refresh when rows show something else, UIList_InsertRows
 * and UIList_RemoveRows when rows came or went (the selection and the current row move with their rows; a removed selected row drops
 * out), UIList_SetProvider for other data altogether. A count that changed with no word is taken as it is: what lies past it drops out.
 *
 * SELECTION, by data index, never by a row's widget: none, one, or several (UISelectionMode). A click selects the row (and only it);
 * with Ctrl it toggles the row, with Shift it selects the range from the anchor (the last row clicked without Shift), with Ctrl and
 * Shift it adds that range. A press on a selected row of several waits for its release to select only it, so the press can drag the
 * selection away. A right press on a row it leaves selected keeps the selection, on another row selects only that row. A click on no
 * row clears the selection. The CURRENT row is where the keys act (the keyboard's cursor), marked while the list has the focus; the
 * clicked row becomes it. Every change the user makes is a whole change: Changed, then Committed, with the number of selected rows as
 * a double; code's changes post nothing.
 *
 * KEYS. The list is an edit element (UIFramework.md, "Entering elements"): focused, it hears only Enter (which enters it) and the short
 * list, so Tab and the arrows pass it; a press on it or Enter enters it. Entered, Up and Down move the current row (Page Up, Page Down a
 * view's height, Home and End to the ends); alone they select the row they reach, with Shift they select from the anchor, with Ctrl they
 * move only the current row. Space selects the current row (Ctrl with it toggles it), the select-all action selects every row, and
 * Enter activates the current row: Activated. Escape and Tab are left to the screen, which leaves the list.
 *
 * EVENTS, each naming its row by data index in the event's value (a UIPropertyType_UInt64): Activated (Enter, or a double click), the
 * double click's DoubleClicked, ContextRequested (a right click on a row, or the menu key on the current row; a context menu provider
 * asks UIList_GetItemAt or UIList_GetCurrent which row), and UIElementEvent_CellToggled when a toggle cell is clicked, its cell the
 * event's index: the cell's state is the caller's data, which the caller changes and refreshes. Pressed and Released tell of the input
 * as for every element.
 *
 * DRAGS. Made a drag source (UIListOptions.IsDragSource, UIList_SetDragSource), its rows are dragged (UIDragDrop.h): a press on a selected
 * row moved past the drag distance drags the selection away (a press on a toggle cell never drags), the payload's value the pressed row, its
 * text the row's text or how many rows go.
 * Made a drop target (UIDragDrop_SetTarget), it shows where a drop would go, between two rows, while it would take it, and scrolls when
 * the pointer nears its top or bottom; UIList_GetDropIndex tells where a dropped point falls. While a drag is on, no row looks hovered.
 *
 * The wheel scrolls it while it can and then goes on up the tree, as a scroll area's. Disabled, it swallows presses, takes no focus and
 * draws its rows dimmed. Its look (UIKit_RegisterLook: a struct starting with UIListLookBase) draws its background, measures it and
 * tells the behaviour the theme's metrics (UIListCapability_SetMetrics); its rows and its bar are the kit's own widgets, drawn from the
 * theme's list kind with the variant of the list's look.
 */


// Forward declarations.
/** @brief The widget factory; full type in UIWidgetFactory.h. */
typedef struct UIWidgetFactoryStruct UIWidgetFactory;


// Macros.
/** @brief The most toggle cells a row has. */
#define UI_LIST_MAX_CELLS ((size_t)4)
/** @brief The most bytes of a row's text that show: a longer text is cut at a codepoint before it. */
#define UI_LIST_MAX_TEXT_BYTES ((size_t)512)
/** @brief How many rows a list measures as tall unless its options say otherwise. */
#define UI_LIST_DEFAULT_VISIBLE_ROWS ((size_t)8)
/** @brief The most rows a list may measure as tall. */
#define UI_LIST_MAX_VISIBLE_ROWS ((size_t)200)
/** @brief How many rows a notch of the wheel scrolls. */
#define UI_LIST_WHEEL_ROWS 3.0f
/** @brief How near the top or the bottom of its rows, in rows, the pointer of a drag must come for the list to scroll, and how many rows a
 *  second it scrolls there at most. */
#define UI_LIST_DRAG_SCROLL_ROWS 1.0f
#define UI_LIST_DRAG_SCROLL_ROWS_PER_SECOND 24.0f


// Types.
/**
 * @brief How many rows a list or a tree lets the user select.
 */
typedef enum UISelectionModeEnum
{
    /** @brief None: clicks and keys move the current row only. */
    UISelectionMode_None,
    /** @brief One at most. */
    UISelectionMode_Single,
    /** @brief Any number, with Shift and Ctrl. */
    UISelectionMode_Multiple
} UISelectionMode;

/**
 * @brief What a row provider writes about one row. The list clears it before each call.
 */
typedef struct UIListRowDataStruct
{
    /** @brief A byte buffer to append the row's UTF-8 text to (no NUL needed); what it appends ends at its first NUL, is cut at
     *  UI_LIST_MAX_TEXT_BYTES, and shows nothing when it is not valid UTF-8. Borrowed for the call. */
    GenericBuffer* Text;
    /** @brief Its icon; none unless the provider sets one (UIElementIcon_FromLetter, UIElementIcon_FromSpriteAnimation). An icon that is
     *  not valid shows none. */
    UIElementIcon Icon;
    /** @brief Whether it shows dimmed (a runtime object under its root, say), in the row's disabled colours. */
    bool IsDimmed;
    /** @brief Whether each of the list's toggle cells is on, the first UIListCapability's cell count of them. */
    bool Cells[UI_LIST_MAX_CELLS];
} UIListRowData;

/**
 * @brief Tells how many rows a list has.
 *
 * Runs on the update side, at each of the list's updates; it must not change widgets, focus or the screen.
 * @param screen The screen.
 * @param listId The list.
 * @param userData The UserData given with the providers.
 * @returns The count.
 */
typedef size_t (*UIListCountProvider)(UIScreen* screen, uint64_t listId, const UserData* userData);

/**
 * @brief Tells what one row shows; asked for the rows in view, when they come into view and after a refresh.
 *
 * Runs on the update side; it must not change widgets, focus or the screen, only write @p outRow.
 * @param screen The screen.
 * @param listId The list.
 * @param userData The UserData given with the providers.
 * @param index The row, below the count it told.
 * @param outRow [out] What the row shows; cleared before the call (no text, no icon, not dimmed, every cell off).
 * @returns Success, or an Error that stops the screen's update as a broken invariant.
 */
typedef Error (*UIListRowProvider)(UIScreen* screen, uint64_t listId, const UserData* userData, size_t index, UIListRowData* outRow);

/**
 * @brief What a list is created with (UIKit_CreateList). A plain value; build one with UIListOptions_Create.
 */
typedef struct UIListOptionsStruct
{
    /** @brief What every element is created with. */
    UIElementOptions Element;
    /** @brief How many rows the user may select. */
    UISelectionMode SelectionMode;
    /** @brief How many rows there are; NULL for none until UIList_SetProvider. Both providers or neither. */
    UIListCountProvider CountProvider;
    /** @brief What a row shows; NULL for none until UIList_SetProvider. */
    UIListRowProvider RowProvider;
    /** @brief Handed to the providers; copied. */
    UserData ProviderData;
    /** @brief How many toggle cells each row has at its right, at most UI_LIST_MAX_CELLS. */
    size_t CellCount;
    /** @brief Each cell's letter or picture, the first CellCount of them; a cell with no icon shows its box alone. */
    UIElementIcon CellIcons[UI_LIST_MAX_CELLS];
    /** @brief How many rows tall it measures, within [1, UI_LIST_MAX_VISIBLE_ROWS] (its preferred height; a placement or a stack's hint
     *  may give it another). */
    size_t VisibleRows;
    /** @brief Whether it takes input. */
    bool IsEnabled;
    /** @brief Whether its rows are dragged. */
    bool IsDragSource;
    /** @brief The kind they are dragged as (UIDragPayload.Kind), when IsDragSource. */
    uint32_t DragKind;
} UIListOptions;

/**
 * @brief What a list look's Construct gets (UIKit_CreateList passes it): the kit and the options.
 */
typedef struct UIListArgsStruct
{
    /** @brief The kit; borrowed. */
    UIKit* Kit;
    /** @brief The options; borrowed for the call. */
    const UIListOptions* Options;
} UIListArgs;

/**
 * @brief One run of selected rows: Count rows from Start. A plain value.
 */
typedef struct UIListRangeStruct
{
    /** @brief The first selected row. */
    size_t Start;
    /** @brief How many, at least 1. */
    size_t Count;
} UIListRange;

/**
 * @brief The theme's sizes a list lays its rows out by, in the list's UI pixels; its look tells them (UIListCapability_SetMetrics).
 */
typedef struct UIListMetricsStruct
{
    /** @brief Inside the list's box, around its rows, horizontal and vertical. */
    Vector2 Padding;
    /** @brief A row's height; at least 1. */
    float RowHeight;
    /** @brief A row's inset at its sides and the gaps between its icon, its text and its cells. */
    float RowPadding;
    /** @brief A toggle cell's side. */
    float CellSize;
    /** @brief How thick the line where a drop would go is. */
    float InsertionThickness;
} UIListMetrics;

/**
 * @brief A list's capability: its data, its selection and its view, no look. Embedded by a look's widget type right after its Widget
 *        (UIListLookBase); its fields are read-only outside the list's module.
 */
typedef struct UIListCapabilityStruct
{
    /** @brief What scrolls: the rows, down. Calling code reaches it by UIScrollable_*. */
    UIScrollableCapability Scrollable;
    /** @brief The providers and what they are handed; both NULL with no data. */
    UIListCountProvider _countProvider;
    UIListRowProvider _rowProvider;
    UserData _providerData;
    /** @brief How many rows there were at the last update. */
    size_t _count;
    /** @brief How many rows the user may select. */
    UISelectionMode _mode;
    /** @brief The selected rows (UIListRange), sorted, apart (never touching), each at least one row. Owned. */
    GenericBuffer _selection;
    /** @brief How many rows the ranges hold together. */
    size_t _selectedCount;
    /** @brief The current row (where the keys act), and whether there is one. */
    bool _hasCurrent;
    size_t _current;
    /** @brief Where a Shift range starts, and whether it is set. */
    bool _hasAnchor;
    size_t _anchor;
    /** @brief The row under the pointer at the last update, and whether there is one. */
    bool _hasHovered;
    size_t _hovered;
    /** @brief The toggle cell under the pointer at the last update, or -1. */
    int32_t _hoveredCell;
    /** @brief The rows' widgets. */
    UIRowRecycler _rows;
    /** @brief The box that cuts the rows, and the scroll bar; made with the list (UIKit_CreateList). */
    uint64_t _rowsBoxId;
    uint64_t _barId;
    /** @brief The sizes it lays its rows out by. */
    UIListMetrics _metrics;
    /** @brief The bar's thickness at the last layout, in the list's UI pixels. */
    float _barThickness;
    /** @brief Whether the bar showed at the last layout. */
    bool _isBarShown;
    /** @brief How many toggle cells each row has, and their icons. */
    size_t _cellCount;
    UIElementIcon _cellIcons[UI_LIST_MAX_CELLS];
    /** @brief How many rows tall it measures. */
    size_t _visibleRows;
    /** @brief Whether it takes input. */
    bool _isEnabled;
    /** @brief Whether its rows are dragged, and as what. */
    bool _isDragSource;
    uint32_t _dragKind;
    /** @brief Whether the current row should be scrolled into view at the next layout. */
    bool _isRevealPending;
    /** @brief A row's text being written. Owned. */
    GenericBuffer _scratch;
    /** @brief The press in progress: on a row (and which), on a cell of it (or -1), with which button, and whether its release selects only
     *  its row (a press on a row already selected among several). */
    bool _hasPress;
    GameKey _pressButton;
    bool _hasPressRow;
    size_t _pressRow;
    int32_t _pressCell;
    bool _isSelectOnRelease;
    /** @brief A right press's row, for its ContextRequested at the release. */
    bool _hasContextRow;
    size_t _contextRow;
    /** @brief Seconds of a drag's scrolling not yet turned into a whole step, and the drag's insertion row (where a drop would go) while
     *  the list is the target that would take it. */
    double _dragScrollSeconds;
    bool _hasInsertion;
    size_t _insertion;
} UIListCapability;

/**
 * @brief How every list look's widget struct starts: the widget, then the capability.
 */
typedef struct UIListLookBaseStruct
{
    /** @brief The widget. */
    Widget Base;
    /** @brief The list's behaviour. */
    UIListCapability List;
} UIListLookBase;


// Helpers.
/**
 * @brief Default options: no data, several rows selectable, no cells, eight rows tall, enabled, no drags.
 * @returns The options.
 */
UIListOptions UIListOptions_Create(void);

/**
 * @brief Returns whether a selection mode is one of the enum's values.
 * @param mode The mode.
 * @returns true if known.
 */
bool UISelectionMode_IsValid(UISelectionMode mode);


// Calling code: by widget id.
/**
 * @brief Gives a list its data: the providers and what they are handed. The selection, the current row and the anchor are forgotten;
 *        the view keeps its offset (clamped).
 * @param screen The list's screen; must not be NULL.
 * @param widgetId The list.
 * @param countProvider How many rows; NULL with @p rowProvider NULL for no data.
 * @param rowProvider What a row shows.
 * @param userData Handed to the providers; may be NULL (empty). Copied.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no list; ErrorCode_IllegalArgument for one provider
 *          without the other.
 */
Error UIList_SetProvider(UIScreen* screen, uint64_t widgetId, UIListCountProvider countProvider, UIListRowProvider rowProvider,
    const UserData* userData);

/**
 * @brief Tells a list its rows show something else, in the same order: the rows in view are asked again at its next update.
 * @param screen The list's screen; must not be NULL.
 * @param widgetId The list.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no list.
 */
Error UIList_Refresh(UIScreen* screen, uint64_t widgetId);

/**
 * @brief Tells a list rows were inserted: @p count rows now stand at @p index, and the rows from there on moved down. The selection, the
 *        current row and the anchor move with their rows (rows inserted inside a selected run are not selected); the rows in view are
 *        asked again.
 * @param screen The list's screen; must not be NULL.
 * @param widgetId The list.
 * @param index Where the first new row stands; at most the count before the insertion.
 * @param count How many.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no list; ErrorCode_ArgumentOutOfRange for an index past
 *          the count; ErrorCode_BufferTooLarge if the selection could not grow (it is left as it was).
 */
Error UIList_InsertRows(UIScreen* screen, uint64_t widgetId, size_t index, size_t count);

/**
 * @brief Tells a list rows were removed: the @p count rows from @p index are gone, and the rows after them moved up. Removed rows drop out
 *        of the selection; the others, the current row and the anchor move with their rows (a removed current row's place goes to the row
 *        that took it, or the last row). Code's change: no event.
 * @param screen The list's screen; must not be NULL.
 * @param widgetId The list.
 * @param index The first removed row; below the count before the removal, or the count with @p count 0.
 * @param count How many; index + count at most the count before the removal.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no list; ErrorCode_ArgumentOutOfRange for rows past the
 *          count.
 */
Error UIList_RemoveRows(UIScreen* screen, uint64_t widgetId, size_t index, size_t count);

/**
 * @brief Selects or deselects one row by code (no event). With one row selectable, selecting it deselects the other.
 * @param screen The list's screen; must not be NULL.
 * @param widgetId The list.
 * @param index The row; below the count.
 * @param isSelected Whether it is selected.
 * @returns Success; ErrorCode_InvalidOperation for a stale id, a widget that is no list, or selecting in a list that selects none;
 *          ErrorCode_ArgumentOutOfRange for a row past the count; ErrorCode_BufferTooLarge if the selection could not grow.
 */
Error UIList_SetSelected(UIScreen* screen, uint64_t widgetId, size_t index, bool isSelected);

/**
 * @brief Selects or deselects a run of rows by code (no event).
 * @param screen The list's screen; must not be NULL.
 * @param widgetId The list.
 * @param first The first row.
 * @param count How many; first + count at most the count. Selecting more than one needs several to be selectable.
 * @param isSelected Whether they are selected.
 * @returns Success; ErrorCode_InvalidOperation for a stale id, a widget that is no list, or more selected than the mode lets;
 *          ErrorCode_ArgumentOutOfRange for rows past the count; ErrorCode_BufferTooLarge if the selection could not grow.
 */
Error UIList_SetRangeSelected(UIScreen* screen, uint64_t widgetId, size_t first, size_t count, bool isSelected);

/**
 * @brief Deselects every row by code (no event).
 * @param screen The list's screen; must not be NULL.
 * @param widgetId The list.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no list.
 */
Error UIList_ClearSelection(UIScreen* screen, uint64_t widgetId);

/**
 * @brief Reads whether a row is selected.
 * @param screen The list's screen; must not be NULL.
 * @param widgetId The list.
 * @param index The row.
 * @param outIsSelected [out] Receives it (false past the count). Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no list.
 */
Error UIList_IsSelected(UIScreen* screen, uint64_t widgetId, size_t index, bool* outIsSelected);

/**
 * @brief Reads how many rows are selected and in how many runs; each run is read with UIList_GetSelectionRange.
 * @param screen The list's screen; must not be NULL.
 * @param widgetId The list.
 * @param outSelectedCount [out] Receives how many rows; may be NULL.
 * @param outRangeCount [out] Receives how many runs; may be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no list.
 */
Error UIList_GetSelection(UIScreen* screen, uint64_t widgetId, size_t* outSelectedCount, size_t* outRangeCount);

/**
 * @brief Reads one run of selected rows, in rising order.
 * @param screen The list's screen; must not be NULL.
 * @param widgetId The list.
 * @param rangeIndex In [0, the run count).
 * @param outRange [out] Receives the run. Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no list; ErrorCode_ArgumentOutOfRange for a run past
 *          the count.
 */
Error UIList_GetSelectionRange(UIScreen* screen, uint64_t widgetId, size_t rangeIndex, UIListRange* outRange);

/**
 * @brief Makes a row the current one by code, and scrolls it into view at the next layout. It becomes the anchor too.
 * @param screen The list's screen; must not be NULL.
 * @param widgetId The list.
 * @param index The row; below the count.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no list; ErrorCode_ArgumentOutOfRange for a row past
 *          the count.
 */
Error UIList_SetCurrent(UIScreen* screen, uint64_t widgetId, size_t index);

/**
 * @brief Reads the current row.
 * @param screen The list's screen; must not be NULL.
 * @param widgetId The list.
 * @param outHasCurrent [out] Receives whether there is one. Must not be NULL.
 * @param outIndex [out] Receives it; may be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no list.
 */
Error UIList_GetCurrent(UIScreen* screen, uint64_t widgetId, bool* outHasCurrent, size_t* outIndex);

/**
 * @brief Scrolls a list as little as it takes to show a row, by the view of its last layout.
 * @param screen The list's screen; must not be NULL.
 * @param widgetId The list.
 * @param index The row; below the count.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no list; ErrorCode_ArgumentOutOfRange for a row past
 *          the count.
 */
Error UIList_Reveal(UIScreen* screen, uint64_t widgetId, size_t index);

/**
 * @brief Tells which row lies at a point, as the list was last laid out: what a context menu provider asks.
 * @param screen The list's screen; must not be NULL.
 * @param widgetId The list.
 * @param screenPosition The point, in normalized screen coordinates.
 * @param outHasRow [out] Receives whether a row lies there. Must not be NULL.
 * @param outIndex [out] Receives it; may be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no list.
 */
Error UIList_GetItemAt(UIScreen* screen, uint64_t widgetId, Vector2 screenPosition, bool* outHasRow, size_t* outIndex);

/**
 * @brief Tells where a drop at a point would go, as the list was last laid out: the row it would stand before (the count for after the
 *        last row), by the nearer edge of the row under the point; above the rows it is 0, below them the count.
 * @param screen The list's screen; must not be NULL.
 * @param widgetId The list.
 * @param screenPosition The point, in normalized screen coordinates (a drop event's place).
 * @param outIndex [out] Receives it. Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no list.
 */
Error UIList_GetDropIndex(UIScreen* screen, uint64_t widgetId, Vector2 screenPosition, size_t* outIndex);

/**
 * @brief Sets how many rows the user may select; a selection the new mode does not allow keeps its first row (none with no selection).
 * @param screen The list's screen; must not be NULL.
 * @param widgetId The list.
 * @param mode The mode.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no list; ErrorCode_ArgumentOutOfRange for an unknown
 *          mode.
 */
Error UIList_SetSelectionMode(UIScreen* screen, uint64_t widgetId, UISelectionMode mode);

/**
 * @brief Sets a list's toggle cells: how many each row has and their icons. Rows are asked again.
 * @param screen The list's screen; must not be NULL.
 * @param widgetId The list.
 * @param count At most UI_LIST_MAX_CELLS.
 * @param icons Each cell's icon, @p count of them; may be NULL for none. Each must be valid (UIElementIcon_IsValid).
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no list; ErrorCode_ArgumentOutOfRange for too many cells
 *          or an icon that is not valid.
 */
Error UIList_SetCells(UIScreen* screen, uint64_t widgetId, size_t count, const UIElementIcon* icons);

/**
 * @brief Sets whether a list's rows are dragged, and as what.
 * @param screen The list's screen; must not be NULL.
 * @param widgetId The list.
 * @param isDragSource Whether they are.
 * @param dragKind The kind they are dragged as (UIDragPayload.Kind), when they are.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no list; otherwise the drag source table's error.
 */
Error UIList_SetDragSource(UIScreen* screen, uint64_t widgetId, bool isDragSource, uint32_t dragKind);

/**
 * @brief Enables or disables a list. Disabling it ends a press and takes its focus away.
 * @param screen The list's screen; must not be NULL.
 * @param widgetId The list.
 * @param isEnabled Whether it takes input.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no list; otherwise a hook's error.
 */
Error UIList_SetIsEnabled(UIScreen* screen, uint64_t widgetId, bool isEnabled);


// Behaviour helpers, for looks.
/**
 * @brief Tells the behaviour the theme's sizes it lays its rows out by; a look calls it from its Update. Values that are not finite
 *        numbers >= 0 count as 0, a row height below 1 as 1; a change lays the rows out again.
 * @param self The capability; must not be NULL.
 * @param widget The list's widget; must not be NULL.
 * @param metrics The sizes; must not be NULL.
 */
void UIListCapability_SetMetrics(UIListCapability* self, Widget* widget, const UIListMetrics* metrics);

/**
 * @brief Returns the state a look shows the list's box in: disabled, entered, focused or normal (hovered under the pointer).
 * @param self The capability; must not be NULL.
 * @param widget The list's widget; must not be NULL.
 * @returns The state.
 */
UIElementState UIListCapability_GetState(const UIListCapability* self, const Widget* widget);

/**
 * @brief Measures a list as its look would: its rows' height for its visible row count, and a width for its cells and a short text,
 *        within its padding, in its own UI pixels.
 * @param self The capability; must not be NULL.
 * @param outSize [out] Receives the size. Must not be NULL.
 */
void UIListCapability_Measure(const UIListCapability* self, Vector2* outSize);

/**
 * @brief Registers the kit's own widget type a list's and a tree's rows are. The kit calls it when it is constructed.
 * @param kit The kit, whose capabilities exist already; must not be NULL.
 * @param factory The kit's factory; must not be NULL.
 * @returns Success, or the factory's error.
 */
Error UIList_RegisterTypes(UIKit* kit, UIWidgetFactory* factory);

/**
 * @brief Gives a list its parts, which UIKit_CreateList made once the list was attached: the box that cuts its rows and its bar; and reads
 *        its count from its provider, so code can select, reveal and change rows before its first update.
 * @param screen The list's screen; must not be NULL.
 * @param widgetId The list.
 * @param rowsBoxId The box.
 * @param barId The bar.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no list.
 */
Error UIList_SetParts(UIScreen* screen, uint64_t widgetId, uint64_t rowsBoxId, uint64_t barId);


// The selection's arithmetic, pure: a sorted list of apart runs (UIListRange) in a GenericBuffer of them.
/**
 * @brief Returns whether a row lies in a sorted list of runs. O(log runs).
 * @param ranges The runs; must not be NULL.
 * @param index The row.
 * @returns true if selected.
 */
bool UIListRanges_Contains(const GenericBuffer* ranges, size_t index);

/**
 * @brief Selects or deselects a run of rows in a sorted list of runs, keeping it sorted and apart (touching runs merge).
 * @param ranges The runs; must not be NULL.
 * @param start The first row.
 * @param count How many; 0 changes nothing.
 * @param isSelected Whether they are selected.
 * @param outCount [in, out] The rows the runs hold together, kept current; must not be NULL.
 * @returns true; false when the list could not grow, with it left as it was.
 */
bool UIListRanges_Set(GenericBuffer* ranges, size_t start, size_t count, bool isSelected, size_t* outCount);

/**
 * @brief Moves the runs for rows inserted: rows from @p index on move down by @p count, and a run that @p index falls inside is split there
 *        (the new rows are not selected).
 * @param ranges The runs; must not be NULL.
 * @param index Where the new rows stand.
 * @param count How many.
 * @returns true; false when the list could not grow, with it left as it was.
 */
bool UIListRanges_Insert(GenericBuffer* ranges, size_t index, size_t count);

/**
 * @brief Moves the runs for rows removed: the removed rows drop out, the rows after them move up by @p count, and runs that come to touch
 *        merge.
 * @param ranges The runs; must not be NULL.
 * @param index The first removed row.
 * @param count How many.
 * @param outCount [in, out] The rows the runs hold together, kept current; must not be NULL.
 */
void UIListRanges_Remove(GenericBuffer* ranges, size_t index, size_t count, size_t* outCount);
