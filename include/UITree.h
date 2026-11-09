#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "wr/WRMemory.h"
#include "wr/WRUserData.h"
#include "UIElement.h"
#include "UIList.h"
#include "UIRowRecycler.h"
#include "UIScrollable.h"
#include "UIWidget.h"


/**
 * @file UITree.h
 * @brief The tree element (kind tree): nodes that live with the caller, shown as rows that nest, only the rows in view made, so a tree of
 *        a hundred thousand nodes costs what the rows on screen cost.
 *
 * DATA. Nodes are the caller's, named by 64-bit ids (an object's id), and come through a provider (UITreeProvider): a node's children
 * (or the top level's), whether a node is expanded and how to expand it (the caller keeps that state, so it outlives the tree: a window
 * closed and opened again shows the same), what a node shows (its text, an icon, dimmed, its toggle cells, as a list's row: UIListRowData),
 * and whether a node still exists. The tree walks the expanded part of the data into rows (a node's children below it, a level deeper),
 * at most UI_TREE_MAX_DEPTH levels and UI_TREE_MAX_ROWS rows, and walks it again only when it must: a node expanded or collapsed, or the
 * caller's word that the data changed (UITree_Refresh). Its rows are recycled widgets, as a list's, at a fixed height.
 *
 * SELECTION, by node id, as a list's by data index: none, one or several, with Ctrl and Shift (a Shift run spans the rows between the anchor
 * and the row), a press on a selected node among several waiting for its release. A node hidden by a collapsed ancestor stays selected; one
 * the provider says is gone drops out at the next refresh. Every change the user makes is a whole change: Changed, then Committed, with the
 * number of selected nodes as a double.
 *
 * EXPANDING. A press on a node's expander (the arrow before it) expands or collapses it; Right expands the current node (or moves to its first
 * child once it is expanded), Left collapses it (or moves to its parent). Collapsing a node whose subtree holds the current row makes the node
 * current. The tree asks the provider to change the node's state (SetExpanded) and shows the result at its next update.
 *
 * KEYS, the rest as a list's: an edit element, entered by Enter or a press; Up, Down, Page Up, Page Down, Home and End move the current
 * row; Space, the select-all action, Enter activating the current node. EVENTS as a list's, each naming its node in the event's value
 * (UIPropertyType_UInt64): Activated, DoubleClicked, ContextRequested, UIElementEvent_CellToggled (its cell the event's index).
 *
 * DRAGS. Made a drag source (UITreeOptions.IsDragSource, UITree_SetDragSource), its nodes are dragged as a list's rows are (a press on an
 * expander or a toggle cell never drags), its payload's value the pressed node; while a drag is on, no row looks hovered. Made a drop target, it shows where a drop would go: between two rows (the
 * line above or below a node), or into a node (a box around it), and UITree_GetDropPlace tells which for a dropped point.
 *
 * Its look (a struct starting with UITreeLookBase) draws its background, measures it and tells the behaviour the theme's metrics
 * (UITreeCapability_SetMetrics); its rows and its bar are the kit's own widgets, drawn from the theme's tree kind.
 */


// Macros.
/** @brief The deepest level shown: deeper nodes are left out (an expander shows on their parent, and opens nothing). */
#define UI_TREE_MAX_DEPTH ((uint32_t)64)
/** @brief The most rows a tree shows: past it the rest of the data is left out, and that is logged once. */
#define UI_TREE_MAX_ROWS ((size_t)1 << 21)


// Types.
/**
 * @brief How a tree reads the caller's nodes. Static data; every hook is required. Each runs on the update side and must not change widgets,
 *        focus or the screen; SetExpanded changes only the caller's own data.
 */
typedef struct UITreeProviderStruct
{
    /** @brief How many children a node has; with @p isRoot, how many nodes the top level has (@p node is then meaningless). */
    size_t (*GetChildCount)(UIScreen* screen, uint64_t treeId, const UserData* userData, bool isRoot, uint64_t node);
    /** @brief A node's child at @p index, below the count it told; with @p isRoot, a node of the top level. */
    uint64_t (*GetChild)(UIScreen* screen, uint64_t treeId, const UserData* userData, bool isRoot, uint64_t node, size_t index);
    /** @brief Whether a node shows its children. */
    bool (*IsExpanded)(UIScreen* screen, uint64_t treeId, const UserData* userData, uint64_t node);
    /** @brief Expands or collapses a node: the caller keeps the state. */
    void (*SetExpanded)(UIScreen* screen, uint64_t treeId, const UserData* userData, uint64_t node, bool isExpanded);
    /** @brief What a node shows; @p outRow is cleared before the call, as a list's (UIListRowData). Returns success, or an Error that stops
     *  the screen's update as a broken invariant. */
    Error (*WriteNode)(UIScreen* screen, uint64_t treeId, const UserData* userData, uint64_t node, UIListRowData* outRow);
    /** @brief Whether a node still exists; asked for the selected nodes at a refresh. */
    bool (*HasNode)(UIScreen* screen, uint64_t treeId, const UserData* userData, uint64_t node);
} UITreeProvider;

/**
 * @brief Where a drop on a tree would go against a node (UITree_GetDropPlace).
 */
typedef enum UITreeDropEdgeEnum
{
    /** @brief Nowhere: the tree has no rows. */
    UITreeDropEdge_None,
    /** @brief Before the node, among its siblings. */
    UITreeDropEdge_Above,
    /** @brief After the node, among its siblings. */
    UITreeDropEdge_Below,
    /** @brief Into the node, as a child. */
    UITreeDropEdge_Into
} UITreeDropEdge;

/**
 * @brief What a tree is created with (UIKit_CreateTree). A plain value; build one with UITreeOptions_Create.
 */
typedef struct UITreeOptionsStruct
{
    /** @brief What every element is created with. */
    UIElementOptions Element;
    /** @brief How many nodes the user may select. */
    UISelectionMode SelectionMode;
    /** @brief How it reads the nodes; static storage (the tree keeps the pointer), or NULL for none until UITree_SetProvider. */
    const UITreeProvider* Provider;
    /** @brief Handed to the provider; copied. */
    UserData ProviderData;
    /** @brief How many toggle cells each row has at its right, at most UI_LIST_MAX_CELLS, and their icons. */
    size_t CellCount;
    UIElementIcon CellIcons[UI_LIST_MAX_CELLS];
    /** @brief How many rows tall it measures, within [1, UI_LIST_MAX_VISIBLE_ROWS]. */
    size_t VisibleRows;
    /** @brief Whether it takes input. */
    bool IsEnabled;
    /** @brief Whether its nodes are dragged. */
    bool IsDragSource;
    /** @brief The kind they are dragged as (UIDragPayload.Kind), when IsDragSource. */
    uint32_t DragKind;
} UITreeOptions;

/**
 * @brief What a tree look's Construct gets (UIKit_CreateTree passes it): the kit and the options.
 */
typedef struct UITreeArgsStruct
{
    /** @brief The kit; borrowed. */
    UIKit* Kit;
    /** @brief The options; borrowed for the call. */
    const UITreeOptions* Options;
} UITreeArgs;

/**
 * @brief One shown node: a row of the tree. A plain value.
 */
typedef struct UITreeRowStruct
{
    /** @brief The node. */
    uint64_t Node;
    /** @brief How deep it stands: 0 for the top level. */
    uint32_t Depth;
    /** @brief Whether it has children, and shows them. */
    bool HasChildren;
    bool IsExpanded;
} UITreeRow;

/**
 * @brief The theme's sizes a tree lays its rows out by, in the tree's UI pixels; its look tells them (UITreeCapability_SetMetrics).
 */
typedef struct UITreeMetricsStruct
{
    /** @brief Inside the tree's box, around its rows, horizontal and vertical. */
    Vector2 Padding;
    /** @brief A row's height; at least 1. */
    float RowHeight;
    /** @brief A row's inset at its sides and the gaps between its parts. */
    float RowPadding;
    /** @brief How far each level moves a row's content in; the expander's column is as wide. */
    float Indent;
    /** @brief A toggle cell's side. */
    float CellSize;
    /** @brief How thick the line where a drop would go is. */
    float InsertionThickness;
} UITreeMetrics;

/**
 * @brief A tree's capability: its data, its rows, its selection and its view, no look. Embedded by a look's widget type right after its
 *        Widget (UITreeLookBase); its fields are read-only outside the tree's module.
 */
typedef struct UITreeCapabilityStruct
{
    /** @brief What scrolls: the rows, down. Calling code reaches it by UIScrollable_*. */
    UIScrollableCapability Scrollable;
    /** @brief How it reads the nodes (borrowed, static), and what the provider is handed. */
    const UITreeProvider* _provider;
    UserData _providerData;
    /** @brief The shown nodes in order (UITreeRow). Owned. */
    GenericBuffer _rows;
    /** @brief Whether the rows must be walked again at the next update. */
    bool _isDirty;
    /** @brief Whether the selected nodes are to be checked against the provider at the next walk (a refresh). */
    bool _isSelectionCheckPending;
    /** @brief Whether the walk's row limit has been logged. */
    bool _isRowLimitReported;
    /** @brief How many nodes the user may select. */
    UISelectionMode _mode;
    /** @brief The selected nodes' ids (uint64_t), rising. Owned. */
    GenericBuffer _selection;
    /** @brief Node ids being gathered, for a run and its merge. Owned. */
    GenericBuffer _gathered;
    /** @brief The current node, and the row it was last seen at (where the current row goes when the node is gone). */
    bool _hasCurrent;
    uint64_t _current;
    size_t _currentRowHint;
    /** @brief Where a Shift run starts. */
    bool _hasAnchor;
    uint64_t _anchor;
    /** @brief The row under the pointer at the last update, its cell (or -1) and whether the pointer is on its expander. */
    bool _hasHovered;
    size_t _hoveredRow;
    int32_t _hoveredCell;
    bool _isExpanderHovered;
    /** @brief The rows' widgets. */
    UIRowRecycler _recycler;
    /** @brief The box that cuts the rows, and the scroll bar; made with the tree (UIKit_CreateTree). */
    uint64_t _rowsBoxId;
    uint64_t _barId;
    /** @brief The sizes it lays its rows out by. */
    UITreeMetrics _metrics;
    /** @brief The bar's thickness at the last layout, and whether it showed. */
    float _barThickness;
    bool _isBarShown;
    /** @brief How many toggle cells each row has, and their icons. */
    size_t _cellCount;
    UIElementIcon _cellIcons[UI_LIST_MAX_CELLS];
    /** @brief How many rows tall it measures. */
    size_t _visibleRows;
    /** @brief Whether it takes input. */
    bool _isEnabled;
    /** @brief Whether its nodes are dragged, and as what. */
    bool _isDragSource;
    uint32_t _dragKind;
    /** @brief Whether the current row should be scrolled into view at the next layout. */
    bool _isRevealPending;
    /** @brief A node's text being written. Owned. */
    GenericBuffer _scratch;
    /** @brief The press in progress: on a node (and which), on a cell of it (or -1), with which button, and whether its release selects only
     *  its node. */
    bool _hasPress;
    GameKey _pressButton;
    bool _hasPressNode;
    uint64_t _pressNode;
    int32_t _pressCell;
    bool _isSelectOnRelease;
    /** @brief A right press's node, for its ContextRequested at the release. */
    bool _hasContextNode;
    uint64_t _contextNode;
    /** @brief A drag's scrolling not yet turned into a whole step, and where a drop would go while the tree would take it: the row and the
     *  edge. */
    double _dragScrollSeconds;
    bool _hasInsertion;
    size_t _insertionRow;
    UITreeDropEdge _insertionEdge;
} UITreeCapability;

/**
 * @brief How every tree look's widget struct starts: the widget, then the capability.
 */
typedef struct UITreeLookBaseStruct
{
    /** @brief The widget. */
    Widget Base;
    /** @brief The tree's behaviour. */
    UITreeCapability Tree;
} UITreeLookBase;


// Helpers.
/**
 * @brief Default options: no data, several nodes selectable, no cells, eight rows tall, enabled, no drags.
 * @returns The options.
 */
UITreeOptions UITreeOptions_Create(void);


// Calling code: by widget id.
/**
 * @brief Gives a tree its data. The selection, the current node and the anchor are forgotten; the rows are walked at its next update.
 * @param screen The tree's screen; must not be NULL.
 * @param widgetId The tree.
 * @param provider How it reads the nodes; static storage, or NULL for none. Every hook is required.
 * @param userData Handed to the provider; may be NULL (empty). Copied.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no tree; ErrorCode_IllegalArgument for a provider
 *          missing a hook.
 */
Error UITree_SetProvider(UIScreen* screen, uint64_t widgetId, const UITreeProvider* provider, const UserData* userData);

/**
 * @brief Tells a tree its data changed: its rows are walked again at its next update, and the selected nodes the provider says are gone
 *        drop out of the selection (no event).
 * @param screen The tree's screen; must not be NULL.
 * @param widgetId The tree.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no tree.
 */
Error UITree_Refresh(UIScreen* screen, uint64_t widgetId);

/**
 * @brief Expands or collapses a node by code (the provider's SetExpanded); the rows are walked again at the next update.
 * @param screen The tree's screen; must not be NULL.
 * @param widgetId The tree.
 * @param node The node.
 * @param isExpanded Whether it shows its children.
 * @returns Success; ErrorCode_InvalidOperation for a stale id, a widget that is no tree, or one with no data.
 */
Error UITree_SetExpanded(UIScreen* screen, uint64_t widgetId, uint64_t node, bool isExpanded);

/**
 * @brief Selects or deselects a node by code (no event). With one selectable, selecting it deselects the other.
 * @param screen The tree's screen; must not be NULL.
 * @param widgetId The tree.
 * @param node The node.
 * @param isSelected Whether it is selected.
 * @returns Success; ErrorCode_InvalidOperation for a stale id, a widget that is no tree, or selecting in a tree that selects none;
 *          ErrorCode_BufferTooLarge if the selection could not grow.
 */
Error UITree_SetSelected(UIScreen* screen, uint64_t widgetId, uint64_t node, bool isSelected);

/**
 * @brief Deselects every node by code (no event).
 * @param screen The tree's screen; must not be NULL.
 * @param widgetId The tree.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no tree.
 */
Error UITree_ClearSelection(UIScreen* screen, uint64_t widgetId);

/**
 * @brief Reads whether a node is selected.
 * @param screen The tree's screen; must not be NULL.
 * @param widgetId The tree.
 * @param node The node.
 * @param outIsSelected [out] Receives it. Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no tree.
 */
Error UITree_IsSelected(UIScreen* screen, uint64_t widgetId, uint64_t node, bool* outIsSelected);

/**
 * @brief Reads how many nodes are selected; each is read with UITree_GetSelectedAt.
 * @param screen The tree's screen; must not be NULL.
 * @param widgetId The tree.
 * @param outCount [out] Receives the count. Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no tree.
 */
Error UITree_GetSelectedCount(UIScreen* screen, uint64_t widgetId, size_t* outCount);

/**
 * @brief Reads one selected node, in rising order of the ids.
 * @param screen The tree's screen; must not be NULL.
 * @param widgetId The tree.
 * @param index In [0, the count).
 * @param outNode [out] Receives it. Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no tree; ErrorCode_ArgumentOutOfRange past the count.
 */
Error UITree_GetSelectedAt(UIScreen* screen, uint64_t widgetId, size_t index, uint64_t* outNode);

/**
 * @brief Makes a node the current one by code (and the anchor), and scrolls its row into view at the next layout once it shows.
 * @param screen The tree's screen; must not be NULL.
 * @param widgetId The tree.
 * @param node The node.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no tree.
 */
Error UITree_SetCurrent(UIScreen* screen, uint64_t widgetId, uint64_t node);

/**
 * @brief Reads the current node.
 * @param screen The tree's screen; must not be NULL.
 * @param widgetId The tree.
 * @param outHasCurrent [out] Receives whether there is one. Must not be NULL.
 * @param outNode [out] Receives it; may be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no tree.
 */
Error UITree_GetCurrent(UIScreen* screen, uint64_t widgetId, bool* outHasCurrent, uint64_t* outNode);

/**
 * @brief Reads the rows a tree shows, as its last walk made them (at its last update).
 * @param screen The tree's screen; must not be NULL.
 * @param widgetId The tree.
 * @param outCount [out] Receives how many rows. Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no tree.
 */
Error UITree_GetRowCount(UIScreen* screen, uint64_t widgetId, size_t* outCount);

/**
 * @brief Reads one row a tree shows.
 * @param screen The tree's screen; must not be NULL.
 * @param widgetId The tree.
 * @param index In [0, the row count).
 * @param outRow [out] Receives it. Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no tree; ErrorCode_ArgumentOutOfRange past the count.
 */
Error UITree_GetRow(UIScreen* screen, uint64_t widgetId, size_t index, UITreeRow* outRow);

/**
 * @brief Tells which node lies at a point, as the tree was last laid out: what a context menu provider asks.
 * @param screen The tree's screen; must not be NULL.
 * @param widgetId The tree.
 * @param screenPosition The point, in normalized screen coordinates.
 * @param outHasNode [out] Receives whether a row lies there. Must not be NULL.
 * @param outNode [out] Receives its node; may be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no tree.
 */
Error UITree_GetItemAt(UIScreen* screen, uint64_t widgetId, Vector2 screenPosition, bool* outHasNode, uint64_t* outNode);

/**
 * @brief Tells where a drop at a point would go, as the tree was last laid out: against the row under it, above it in its top quarter,
 *        below it in its bottom quarter, into it between them; above the rows above the first, below them below the last.
 * @param screen The tree's screen; must not be NULL.
 * @param widgetId The tree.
 * @param screenPosition The point, in normalized screen coordinates (a drop event's place).
 * @param outNode [out] Receives the node it goes against. Must not be NULL.
 * @param outEdge [out] Receives where against it; UITreeDropEdge_None with no rows. Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no tree.
 */
Error UITree_GetDropPlace(UIScreen* screen, uint64_t widgetId, Vector2 screenPosition, uint64_t* outNode, UITreeDropEdge* outEdge);

/**
 * @brief Sets how many nodes the user may select; a selection the new mode does not allow keeps its first node (none with no selection).
 * @param screen The tree's screen; must not be NULL.
 * @param widgetId The tree.
 * @param mode The mode.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no tree; ErrorCode_ArgumentOutOfRange for an unknown
 *          mode.
 */
Error UITree_SetSelectionMode(UIScreen* screen, uint64_t widgetId, UISelectionMode mode);

/**
 * @brief Sets whether a tree's nodes are dragged, and as what.
 * @param screen The tree's screen; must not be NULL.
 * @param widgetId The tree.
 * @param isDragSource Whether they are.
 * @param dragKind The kind they are dragged as (UIDragPayload.Kind), when they are.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no tree; otherwise the drag source table's error.
 */
Error UITree_SetDragSource(UIScreen* screen, uint64_t widgetId, bool isDragSource, uint32_t dragKind);

/**
 * @brief Enables or disables a tree. Disabling it ends a press and takes its focus away.
 * @param screen The tree's screen; must not be NULL.
 * @param widgetId The tree.
 * @param isEnabled Whether it takes input.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no tree; otherwise a hook's error.
 */
Error UITree_SetIsEnabled(UIScreen* screen, uint64_t widgetId, bool isEnabled);


// Behaviour helpers, for looks.
/**
 * @brief Tells the behaviour the theme's sizes it lays its rows out by; a look calls it from its Update. Values that are not finite numbers
 *        >= 0 count as 0, a row height below 1 as 1.
 * @param self The capability; must not be NULL.
 * @param widget The tree's widget; must not be NULL.
 * @param metrics The sizes; must not be NULL.
 */
void UITreeCapability_SetMetrics(UITreeCapability* self, Widget* widget, const UITreeMetrics* metrics);

/**
 * @brief Returns the state a look shows the tree's box in: disabled, entered, focused, hovered or normal.
 * @param self The capability; must not be NULL.
 * @param widget The tree's widget; must not be NULL.
 * @returns The state.
 */
UIElementState UITreeCapability_GetState(const UITreeCapability* self, const Widget* widget);

/**
 * @brief Measures a tree as its look would: its rows' height for its visible row count, and a width for a few levels, its cells and a short
 *        text, within its padding, in its own UI pixels.
 * @param self The capability; must not be NULL.
 * @param outSize [out] Receives the size. Must not be NULL.
 */
void UITreeCapability_Measure(const UITreeCapability* self, Vector2* outSize);

/**
 * @brief Gives a tree its parts, which UIKit_CreateTree made once the tree was attached: the box that cuts its rows and its bar.
 * @param self The tree's capability; must not be NULL.
 * @param rowsBoxId The box.
 * @param barId The bar.
 */
void UITree_SetParts(UITreeCapability* self, uint64_t rowsBoxId, uint64_t barId);
