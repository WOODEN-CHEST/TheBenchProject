#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "UIElement.h"
#include "UIList.h"
#include "UIWidget.h"


/*
 * Private to the list and the tree (UIList.c, UITree.c): the kit's own widget type their rows are. A row only shows: it takes no input
 * (its owner hit-tests rows and their cells by the same geometry the row draws with), and its owner tells it what it shows when it binds
 * it (UIListRow_Bind) and how it stands at each of its updates (UIListRow_SetView), which runs before the row's own (parents first).
 *
 * A row, from its left: its padding, the indent of its depth, the expander's column (a tree's: an arrow where the node has children),
 * the icon, the text, and at its right the toggle cells; the current row's marker and the line where a drop would go on top. Drawn from
 * the theme's list or tree kind with the variant of the owner's look.
 */


// Types.
/** Which parts of the theme a row draws with: the list kind's or the tree kind's (Expander -1 for the list). */
typedef struct UIListRowPartsStruct
{
    UIElementKind Kind;
    int32_t Row;
    int32_t Current;
    int32_t Text;
    int32_t Cell;
    int32_t CellText;
    int32_t Insertion;
    int32_t Expander;
} UIListRowParts;

/** Where a drop would go against a row. */
typedef enum UIListRowInsertionEnum
{
    UIListRowInsertion_None,
    UIListRowInsertion_Above,
    UIListRowInsertion_Below,
    /** Into it (a tree's node takes the drop as a child). */
    UIListRowInsertion_Into
} UIListRowInsertion;

/** The sizes a row lays its content out by, in its owner's UI pixels. */
typedef struct UIListRowGeometryStruct
{
    float RowPadding;
    float CellSize;
    float InsertionThickness;
    /** How far a level of depth moves the content in; 0 for a list. */
    float Indent;
    /** Whether the row keeps a column as wide as the indent for an expander (a tree's rows). */
    bool HasExpanderColumn;
} UIListRowGeometry;

/** How a row stands this update, as its owner tells it. */
typedef struct UIListRowViewStruct
{
    bool IsEnabled;
    bool IsSelected;
    bool IsHovered;
    /** It is the current row and its owner has the focus: the marker shows, entered while its owner is. */
    bool IsCurrentShown;
    bool IsOwnerEntered;
    /** The cell under the pointer and the cell pressed, or -1. */
    int32_t HoveredCell;
    int32_t PressedCell;
    /** Whether the pointer is over its expander. */
    bool IsExpanderHovered;
    UIListRowInsertion Insertion;
} UIListRowView;

/** What a row shows, as its owner binds it. */
typedef struct UIListRowContentStruct
{
    /** The text (UTF-8, NUL-terminated); copied. */
    const unsigned char* Text;
    UIElementIcon Icon;
    bool IsDimmed;
    size_t CellCount;
    /** The cells' icons and states, CellCount of each; copied. */
    const UIElementIcon* CellIcons;
    const bool* Cells;
    /** A tree's: its depth, and whether its node has children and is expanded. */
    uint32_t Depth;
    bool HasChildren;
    bool IsExpanded;
} UIListRowContent;


// Functions.
/** The list kind's parts, and the tree kind's. */
const UIListRowParts* UIListRow_GetListParts(void);
const UIListRowParts* UIListRow_GetTreeParts(void);

/** Registers the row's widget type (the kit, when it is constructed). */
Error UIListRow_RegisterType(UIKit* kit, UIWidgetFactory* factory, uint64_t* outTypeId);

/** Makes a row, drawn by @p parts with @p variant (NULL for the kind's own), initialized and attached under @p parentId. On a failure
 *  nothing stays behind. */
Error UIListRow_Create(UIScreen* screen, UIKit* kit, uint64_t parentId, const UIListRowParts* parts, const unsigned char* variant,
    uint64_t* outId);

/** Shows @p content from now on. A row that is gone does nothing. */
Error UIListRow_Bind(UIScreen* screen, uint64_t rowId, const UIListRowContent* content);

/** Shows nothing from now on. */
void UIListRow_Unbind(UIScreen* screen, uint64_t rowId);

/** Tells a row how it stands and the sizes it lays out by. */
void UIListRow_SetView(UIScreen* screen, uint64_t rowId, const UIListRowView* view, const UIListRowGeometry* geometry);

/** What a row shows and how it stands, for tests: its text (NUL-terminated, valid until it is bound again), whether it is bound, and its view.
 *  False for a widget that is gone or no row; any out pointer may be NULL. */
bool UIListRow_Peek(UIScreen* screen, uint64_t rowId, const unsigned char** outText, bool* outIsBound, UIListRowView* outView);

/** Where a row's content starts from its left at a depth, in UI pixels: its padding and its indent (the expander's column starts there). */
float UIListRow_GetContentStart(const UIListRowGeometry* geometry, uint32_t depth);

/** The box of a row's expander at a depth, in UI pixels from the row's top-left. */
Rectangle UIListRow_GetExpanderBox(const UIListRowGeometry* geometry, uint32_t depth, float rowHeight);

/** The box of one of a row's toggle cells, in UI pixels from the row's top-left: the cells stand at the row's right, the last at its
 *  padding from the edge, square, centred down. */
Rectangle UIListRow_GetCellBox(const UIListRowGeometry* geometry, float rowWidth, float rowHeight, size_t cellCount, size_t cell);

/** The toggle cell at a point of a row (UI pixels from its top-left), or -1. */
int32_t UIListRow_FindCell(const UIListRowGeometry* geometry, float rowWidth, float rowHeight, size_t cellCount, Vector2 point);
