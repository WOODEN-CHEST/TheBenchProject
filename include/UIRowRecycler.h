#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
// GenericBuffer holds the rows.
#include "wr/WRMemory.h"


/**
 * @file UIRowRecycler.h
 * @brief Widgets for the rows in view only, reused as the view scrolls: what a dropdown's list, a list and a tree stand on, so a
 *        hundred thousand rows of data cost what the rows on screen cost.
 *
 * The rows are fixed in height. The recycler keeps a few row widgets, each bound to a data row or to none. Each update (its Sync, from
 * the owner's Update, where widgets may be made) works out which data rows the view shows: a widget whose row is still in view keeps it
 * (so a row under the pointer stays hovered while the view scrolls), the others are bound to the rows that came into view, and new
 * widgets are made only when more rows show than ever before. Each layout (its Place, from the owner's Layout) puts every bound widget
 * at its row's place under the view's offset and gives the others no size. The owner makes, binds and unbinds the widgets through hooks;
 * the recycler knows nothing of what a row shows.
 *
 * Cost per update: nothing when the view, the row count and the widgets are as they were; otherwise a pass over the row widgets. Nothing
 * is allocated once the most rows a view showed have widgets.
 */


// Forward declarations.
/** @brief A UI screen; full type in UIScreen.h. */
typedef struct UIScreenStruct UIScreen;


// Macros.
/** @brief The most row widgets a recycler keeps: a view taller than this many rows shows only these. */
#define UI_ROW_RECYCLER_MAX_ROWS ((size_t)256)


// Types.
/**
 * @brief One row widget and the data row it shows.
 */
typedef struct UIRecycledRowStruct
{
    /** @brief The row widget. */
    uint64_t WidgetId;
    /** @brief The data row it shows, when IsBound. */
    size_t DataIndex;
    /** @brief Whether it shows a data row. */
    bool IsBound;
} UIRecycledRow;

/**
 * @brief How the recycler makes and fills the owner's row widgets. Static data; every hook is required. Each may run hooks of its own and
 *        change the tree, as a widget's Update may.
 */
typedef struct UIRowRecyclerHooksStruct
{
    /** @brief Makes a row widget, attached where the rows go, and gives its id. */
    Error (*CreateRow)(void* owner, uint64_t* outWidgetId);
    /** @brief The row widget shows data row @p dataIndex from now on (it may have shown another). */
    Error (*BindRow)(void* owner, uint64_t widgetId, size_t dataIndex);
    /** @brief The row widget shows nothing from now on. */
    Error (*UnbindRow)(void* owner, uint64_t widgetId);
} UIRowRecyclerHooks;

/**
 * @brief The recycler. Construct with UIRowRecycler_Construct and release with UIRowRecycler_Deconstruct (its widgets are the owner's to
 *        destroy, usually with the owner). Fields are read-only.
 */
typedef struct UIRowRecyclerStruct
{
    /** @brief The row widgets (UIRecycledRow). Owned. */
    GenericBuffer _rows;
    /** @brief The rows the last Sync showed, and whether there was one: the same rows again do nothing (the data changing under them is
     *  UIRowRecycler_Invalidate's). */
    bool _hasSynced;
    size_t _syncedFirst;
    size_t _syncedCount;
} UIRowRecycler;


// Functions.
/**
 * @brief Starts a recycler with no rows.
 * @param self The recycler; must not be NULL.
 */
void UIRowRecycler_Construct(UIRowRecycler* self);

/**
 * @brief Releases the recycler's list (not the widgets).
 * @param self The recycler; must not be NULL.
 */
void UIRowRecycler_Deconstruct(UIRowRecycler* self);

/**
 * @brief Works out which rows a view shows: every row any part of which lies in it. Pure.
 * @param rowCount How many rows there are.
 * @param rowHeight A row's height; a value that is not > 0 shows none.
 * @param offset How far the view is scrolled from the first row's top.
 * @param viewHeight The view's height.
 * @param outFirst [out] Receives the first row shown. Must not be NULL.
 * @param outCount [out] Receives how many are shown, at most UI_ROW_RECYCLER_MAX_ROWS. Must not be NULL.
 */
void UIRowRecycler_GetVisibleRange(size_t rowCount, float rowHeight, float offset, float viewHeight, size_t* outFirst, size_t* outCount);

/**
 * @brief Brings the row widgets in line with a view: rows still in view keep their widgets, the rest are bound to the rows that came into
 *        view, widgets are made when more rows show than there are widgets, and widgets left over show nothing. Widgets that were
 *        destroyed are forgotten. Call it from the owner's Update.
 * @param self The recycler; must not be NULL.
 * @param screen The owner's screen; must not be NULL.
 * @param rowCount How many data rows there are.
 * @param rowHeight A row's height.
 * @param offset How far the view is scrolled.
 * @param viewHeight The view's height.
 * @param hooks The owner's hooks; must not be NULL.
 * @param owner Handed to the hooks.
 * @returns Success; ErrorCode_BufferTooLarge if the list could not grow; otherwise a hook's error.
 */
Error UIRowRecycler_Sync(UIRowRecycler* self, UIScreen* screen, size_t rowCount, float rowHeight, float offset, float viewHeight,
    const UIRowRecyclerHooks* hooks, void* owner);

/**
 * @brief Makes the next Sync bind every row widget again, even with the view as it was: the data changed under it.
 * @param self The recycler; must not be NULL.
 */
void UIRowRecycler_Invalidate(UIRowRecycler* self);

/**
 * @brief Places the row widgets in their parent: each bound one at its row's place, @p offset above the parent's top, the full width; the
 *        others with no size. Call it from the owner's Layout.
 * @param self The recycler; must not be NULL.
 * @param screen The owner's screen; must not be NULL.
 * @param rowHeight A row's height, in the parent's UI pixels.
 * @param offset How far the view is scrolled, in the parent's UI pixels.
 * @returns Success, or a placement's error.
 */
Error UIRowRecycler_Place(const UIRowRecycler* self, UIScreen* screen, float rowHeight, float offset);

/**
 * @brief Returns how many row widgets there are.
 * @param self The recycler; must not be NULL.
 * @returns The count.
 */
static inline size_t UIRowRecycler_GetCount(const UIRowRecycler* self)
{
    return self->_rows._count;
}

/**
 * @brief Returns one row widget and what it shows.
 * @param self The recycler; must not be NULL.
 * @param index In [0, UIRowRecycler_GetCount).
 * @returns The row; NULL for an index out of range.
 */
const UIRecycledRow* UIRowRecycler_GetAt(const UIRowRecycler* self, size_t index);

/**
 * @brief Finds the row widget that shows a data row.
 * @param self The recycler; must not be NULL.
 * @param dataIndex The data row.
 * @param outWidgetId [out] Receives the widget when found; must not be NULL.
 * @returns true when a widget shows it.
 */
bool UIRowRecycler_FindWidget(const UIRowRecycler* self, size_t dataIndex, uint64_t* outWidgetId);
