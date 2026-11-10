#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "wr/WRMemory.h"
#include "UIDockTree.h"


/**
 * @file UILayouts.h
 * @brief Named layouts of a workspace's windows (UIWorkspace.h), as pure data, and the file that keeps them across restarts. No widgets:
 *        the workspace captures its arrangement into a layout and shows a layout again; this module keeps them, reads and writes them.
 *
 * A LAYOUT (UILayout) is a name, the windows by their types' keys (each with its floating box in the screen's UI pixels, minimized or
 * maximized), the dock tree whose tabs are those windows (item = the window's index + 1; a window in the tree is docked, one maximized
 * and docked is maximized out of the dock), and where each window type's last window was when it closed (its places). Ratios are
 * fractions and boxes UI pixels, so a layout fits any surface: the workspace keeps floating windows reachable.
 *
 * THE LAYOUTS (UILayouts) are up to UI_LAYOUTS_MAX_COUNT named layouts, one of them active, kept in one JSON file of the player's
 * (references/layouts_structure.md), at a path the host gives. Saving writes the whole file
 * atomically (a temporary file next to it, then a replace). It is written when the arrangement's changes have settled
 * (UILayouts_NoteChange, UILayouts_IsSaveDue: UI_LAYOUTS_SETTLE_SECONDS without a change) and when the host goes, never per tick.
 *
 * THE FILE IS HOSTILE: a player edits it by hand and shares it. Read with limits; a document that is not what the writer writes (bad JSON,
 * wrong types, absurd numbers, a dock deeper or larger than a tree holds, duplicates, another version) is BAD: it is renamed aside to
 * `<path>.bad` (replacing an older one) with one log line, and the host gets no layouts, so it shows its default. Keys it does not know
 * are passed over with one log line; windows and places of window types that are not registered are dropped with one log line, their
 * tabs taken out of the dock; a second window of a type that opens once is dropped.
 *
 * Main thread only.
 */


// Forward declarations.
/** @brief The window types; full type in UIWindowTypes.h. */
typedef struct UIWindowTypesStruct UIWindowTypes;
/** @brief The program's key registry; full type in NamespacedKey.h. */
typedef struct KeyRegistryStruct KeyRegistry;
/** @brief The logger; full type in Logger.h. */
typedef struct LoggerStruct Logger;


// Macros.
/** @brief The most layouts a file holds. */
#define UI_LAYOUTS_MAX_COUNT ((size_t)16)
/** @brief The longest layout name, in bytes of UTF-8. */
#define UI_LAYOUT_MAX_NAME_BYTES ((size_t)64)
/** @brief Room for a layout's name and its terminator. */
#define UI_LAYOUT_NAME_CAPACITY (UI_LAYOUT_MAX_NAME_BYTES + 1U)
/** @brief The most windows a layout holds: as many as a dock's tabs. */
#define UI_LAYOUT_MAX_WINDOWS ((size_t)UI_DOCK_TREE_MAX_TABS)
/** @brief The most window types' places a layout holds. */
#define UI_LAYOUT_MAX_PLACES ((size_t)128)
/** @brief The file's format version, the one this game writes and reads. */
#define UI_LAYOUTS_FORMAT_VERSION 1
/** @brief The longest file read, in bytes. */
#define UI_LAYOUTS_MAX_FILE_BYTES ((size_t)(1024U * 1024U))
/** @brief How long the arrangement stays unchanged before the layouts are saved, in seconds. */
#define UI_LAYOUTS_SETTLE_SECONDS 1.0
/** @brief What a bad file's name gets when it is put aside. */
#define UI_LAYOUTS_BAD_SUFFIX ((const unsigned char*)u8".bad")


// Types.
/**
 * @brief One window of a layout. A plain value.
 */
typedef struct UILayoutWindowStruct
{
    /** @brief Its window type's key, an id of the program's key registry. */
    uint32_t TypeKeyId;
    /** @brief Its floating box in the screen's UI pixels: where it floats, or where it goes back to out of the dock. */
    Rectangle Box;
    /** @brief Whether it is minimized (never while docked). */
    bool IsMinimized;
    /** @brief Whether it shows maximized (out of the dock, when it is docked). At most one window of a layout is. */
    bool IsMaximized;
} UILayoutWindow;

/**
 * @brief Where a window type's last window was when it closed. A plain value.
 */
typedef struct UILayoutPlaceStruct
{
    /** @brief The window type's key, an id of the program's key registry. */
    uint32_t TypeKeyId;
    /** @brief The window's floating box, in the screen's UI pixels. */
    Rectangle Box;
} UILayoutPlace;

/**
 * @brief A layout (above). A plain value with no allocations: copy it whole. Build one with UILayout_Construct and the functions below,
 *        or capture one from a workspace.
 */
typedef struct UILayoutStruct
{
    /** @brief Its name, NUL-terminated UTF-8 (UILayout_IsNameValid). */
    unsigned char Name[UI_LAYOUT_NAME_CAPACITY];
    /** @brief The dock: its items are window indices + 1. */
    UIDockTree Tree;
    /** @brief The windows, WindowCount of them. */
    UILayoutWindow Windows[UI_LAYOUT_MAX_WINDOWS];
    size_t WindowCount;
    /** @brief The window types' places, PlaceCount of them, one per type at most. */
    UILayoutPlace Places[UI_LAYOUT_MAX_PLACES];
    size_t PlaceCount;
} UILayout;

/**
 * @brief What reading the file found.
 */
typedef enum UILayoutsLoadResultEnum
{
    /** @brief The file was read: its layouts are the layouts now. */
    UILayoutsLoadResult_Loaded,
    /** @brief There is no file: nothing changed. */
    UILayoutsLoadResult_Missing,
    /** @brief The file was bad: it was put aside and there are no layouts now. */
    UILayoutsLoadResult_Bad,
    /** @brief The file is there but could not be read (no permission, a lock, an I/O error): it is left as it is, nothing is saved over it
     *  until a load reads it, and there are no layouts now. */
    UILayoutsLoadResult_Unreadable
} UILayoutsLoadResult;

/**
 * @brief The layouts (above). Construct with UILayouts_Construct and release with UILayouts_Deconstruct; fields are internal.
 */
typedef struct UILayoutsStruct
{
    /** @brief The layouts (UILayout), in order. Owned. */
    GenericBuffer _layouts;
    /** @brief The active one's index; meaningful while there are layouts. */
    size_t _activeIndex;
    /** @brief The file's path, NUL-terminated (bytes). Owned. */
    GenericBuffer _path;
    /** @brief The window types the layouts name; borrowed. */
    UIWindowTypes* _types;
    /** @brief The program's key registry, which the types' keys are interned in; borrowed. */
    KeyRegistry* _keys;
    /** @brief Where problems are reported, once each; borrowed, may be NULL. */
    Logger* _logger;
    /** @brief Whether the arrangement changed since the last save, and when it last did (seconds). */
    bool _hasChange;
    double _changeTime;
    /** @brief Whether a failed save was logged: the next ones stay quiet until one succeeds. */
    bool _isSaveFailureLogged;
    /** @brief Whether the file was there but could not be read at the last load: saving is refused, so the player's file is not lost. */
    bool _isSaveBlocked;
} UILayouts;


// A layout.
/**
 * @brief Makes an empty layout: no name, no windows, an empty dock, no places.
 * @param self The layout; must not be NULL.
 */
void UILayout_Construct(UILayout* self);

/**
 * @brief Returns whether a text can name a layout: 1 to UI_LAYOUT_MAX_NAME_BYTES bytes of valid UTF-8, no control characters.
 * @param name The text; NULL is not valid.
 * @returns true when it can.
 */
bool UILayout_IsNameValid(const unsigned char* name);

/**
 * @brief Names a layout.
 * @param self The layout; must not be NULL.
 * @param name The name; must be valid (UILayout_IsNameValid).
 * @returns Success; ErrorCode_ArgumentOutOfRange for a name that is not valid.
 */
Error UILayout_SetName(UILayout* self, const unsigned char* name);

/**
 * @brief Adds a window to a layout (not docked; dock it through its Tree with the index + 1).
 * @param self The layout; must not be NULL.
 * @param typeKeyId Its window type's key id.
 * @param box Its floating box: finite, within UI_LAYOUT_MAX_LENGTH of the origin, its size above 0.
 * @param isMinimized Whether it is minimized.
 * @param isMaximized Whether it shows maximized.
 * @param outIndex [out] Receives its index; may be NULL.
 * @returns Success; ErrorCode_ArgumentOutOfRange for a box that is not valid; ErrorCode_InvalidOperation for a full layout.
 */
Error UILayout_AddWindow(UILayout* self, uint32_t typeKeyId, Rectangle box, bool isMinimized, bool isMaximized, size_t* outIndex);

/**
 * @brief Sets where a window type's last window was (one place per type: an earlier one is replaced).
 * @param self The layout; must not be NULL.
 * @param typeKeyId The window type's key id.
 * @param box The window's floating box: as UILayout_AddWindow's.
 * @returns Success; ErrorCode_ArgumentOutOfRange for a box that is not valid; ErrorCode_InvalidOperation for a full layout.
 */
Error UILayout_SetPlace(UILayout* self, uint32_t typeKeyId, Rectangle box);

/**
 * @brief Returns whether a layout's window is docked (its tab in the dock).
 * @param self The layout; must not be NULL.
 * @param index The window's index.
 * @returns true when docked.
 */
bool UILayout_IsWindowDocked(const UILayout* self, size_t index);

/**
 * @brief Checks a layout whole: a valid name, valid boxes, a whole dock without empty areas whose items are its windows (each at most
 *        once), no docked window minimized, at most one maximized, one place per type.
 * @param self The layout; must not be NULL.
 * @returns true when whole.
 */
bool UILayout_IsValid(const UILayout* self);

/**
 * @brief Compares two layouts by what they mean: the names, the windows in order with their boxes and states, the docks node by node from
 *        their roots (splits, ratios, areas' tabs in order and selections), the places as sets. Node indices may differ.
 * @param a A layout; must not be NULL.
 * @param b Another; must not be NULL.
 * @returns true when they are the same arrangement.
 */
bool UILayout_Equals(const UILayout* a, const UILayout* b);


// The layouts.
/**
 * @brief Makes the layouts, empty, for a file.
 * @param self The layouts; must not be NULL.
 * @param path The file's path, UTF-8; must not be NULL. Copied.
 * @param types The window types the layouts name; borrowed, must outlive the layouts. Must not be NULL.
 * @param keys The program's key registry; borrowed, must outlive the layouts. Must not be NULL.
 * @param logger Reports problems with the file once each; borrowed, may be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a required pointer is NULL.
 */
Error UILayouts_Construct(UILayouts* self, const unsigned char* path, UIWindowTypes* types, KeyRegistry* keys, Logger* logger);

/**
 * @brief Releases the layouts. Saves nothing: a host saves first (UIWorkspace_SaveLayouts).
 * @param self The layouts, or NULL.
 */
void UILayouts_Deconstruct(UILayouts* self);

/**
 * @brief Returns how many layouts there are.
 * @param self The layouts; must not be NULL.
 * @returns The count.
 */
static inline size_t UILayouts_GetCount(const UILayouts* self)
{
    return self->_layouts._count;
}

/**
 * @brief Reads a layout.
 * @param self The layouts; must not be NULL.
 * @param index Its index.
 * @returns The layout, valid until the layouts change; NULL for an index they do not have.
 */
const UILayout* UILayouts_Get(const UILayouts* self, size_t index);

/**
 * @brief Returns the active layout's index.
 * @param self The layouts; must not be NULL.
 * @returns The index; 0 with no layouts.
 */
static inline size_t UILayouts_GetActiveIndex(const UILayouts* self)
{
    return (self->_layouts._count > 0U) ? self->_activeIndex : 0U;
}

/**
 * @brief Makes a layout the active one (what the workspace shows; switching is the workspace's, UIWorkspace_SwitchLayout).
 * @param self The layouts; must not be NULL.
 * @param index Its index.
 * @returns Success; ErrorCode_ArgumentOutOfRange for an index they do not have.
 */
Error UILayouts_SetActiveIndex(UILayouts* self, size_t index);

/**
 * @brief Finds a layout by its name (bytes compared).
 * @param self The layouts; must not be NULL.
 * @param name The name; must not be NULL.
 * @param outIndex [out] Receives its index. Must not be NULL.
 * @returns true when found.
 */
bool UILayouts_FindByName(const UILayouts* self, const unsigned char* name, size_t* outIndex);

/**
 * @brief Adds a layout, last.
 * @param self The layouts; must not be NULL.
 * @param layout The layout: whole (UILayout_IsValid), its name no other's. Copied.
 * @param outIndex [out] Receives its index; may be NULL.
 * @returns Success; ErrorCode_ArgumentOutOfRange for a layout that is not whole or a name that is taken; ErrorCode_InvalidOperation for
 *          as many layouts as there may be.
 */
Error UILayouts_Add(UILayouts* self, const UILayout* layout, size_t* outIndex);

/**
 * @brief Keeps another arrangement in a layout, its name staying as it is.
 * @param self The layouts; must not be NULL.
 * @param index The layout's index.
 * @param layout The arrangement: whole but for its name (which is not read). Copied.
 * @returns Success; ErrorCode_ArgumentOutOfRange for an index they do not have or an arrangement that is not whole.
 */
Error UILayouts_Store(UILayouts* self, size_t index, const UILayout* layout);

/**
 * @brief Renames a layout.
 * @param self The layouts; must not be NULL.
 * @param index Its index.
 * @param name The name: valid (UILayout_IsNameValid), no other layout's.
 * @returns Success; ErrorCode_ArgumentOutOfRange for an index they do not have, or a name that is not valid or taken.
 */
Error UILayouts_Rename(UILayouts* self, size_t index, const unsigned char* name);

/**
 * @brief Takes a layout away. The active index follows its layout; removing the active one makes the one before it active (or the one
 *        after, for the first).
 * @param self The layouts; must not be NULL.
 * @param index Its index.
 * @returns Success; ErrorCode_ArgumentOutOfRange for an index they do not have.
 */
Error UILayouts_Remove(UILayouts* self, size_t index);

/**
 * @brief Writes a name no layout has: @p base, or @p base with a number after it ("Layout 2").
 * @param self The layouts; must not be NULL.
 * @param base The name's start: a valid name short enough for a number after it; must not be NULL.
 * @param outName [out] Receives the name. Must not be NULL, UI_LAYOUT_NAME_CAPACITY bytes.
 */
void UILayouts_MakeName(const UILayouts* self, const unsigned char* base, unsigned char* outName);


// Saving when the changes settle.
/**
 * @brief Notes that the arrangement changed: the layouts are saved once UI_LAYOUTS_SETTLE_SECONDS pass without another change.
 * @param self The layouts; must not be NULL.
 * @param timeSeconds The time now, any steady clock in seconds.
 */
void UILayouts_NoteChange(UILayouts* self, double timeSeconds);

/**
 * @brief Returns whether a change has settled and the layouts are due to be saved.
 * @param self The layouts; must not be NULL.
 * @param timeSeconds The time now, on the clock UILayouts_NoteChange was given.
 * @returns true when due.
 */
bool UILayouts_IsSaveDue(const UILayouts* self, double timeSeconds);

/**
 * @brief Returns whether a change was noted and not saved yet.
 * @param self The layouts; must not be NULL.
 * @returns true when one waits.
 */
static inline bool UILayouts_HasChange(const UILayouts* self)
{
    return self->_hasChange;
}


// The file.
/**
 * @brief Reads the layouts from a document, all or nothing (above, THE FILE IS HOSTILE).
 * @param self The layouts; must not be NULL. Unchanged on a failure.
 * @param bytes The document; may be NULL only when @p byteCount is 0.
 * @param byteCount Its length in bytes.
 * @returns Success; ErrorCode_InvalidJSON for a document that is bad, naming what is wrong.
 */
Error UILayouts_ReadJSON(UILayouts* self, const unsigned char* bytes, size_t byteCount);

/**
 * @brief Writes the layouts as a document (references/layouts_structure.md), appended to a byte buffer.
 * @param self The layouts; must not be NULL.
 * @param outBytes [out] The byte buffer it is appended to. Must not be NULL.
 * @returns Success; otherwise the JSON writer's error.
 */
Error UILayouts_WriteJSON(const UILayouts* self, GenericBuffer* outBytes);

/**
 * @brief Reads the layouts' file: a missing file changes nothing; a bad one (not what the writer writes) is put aside as `<path>.bad`,
 *        logged once, and leaves no layouts (the host shows its default); one that is there but cannot be read (no permission, a lock,
 *        an I/O error) is left as it is, logged once, leaves no layouts, and blocks saving until a load reads it.
 * @param self The layouts; must not be NULL.
 * @param outResult [out] Receives what it found; may be NULL.
 * @returns Success for each of those; an error only when a bad file could not be put aside (it stays where it is, and there are no
 *          layouts either way).
 */
Error UILayouts_Load(UILayouts* self, UILayoutsLoadResult* outResult);

/**
 * @brief Writes the layouts' file atomically and forgets the change waiting. A failure is logged once until a save succeeds, and the
 *        file stays as it was. Refused while the last load found the file there but unreadable.
 * @param self The layouts; must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation while saving is blocked (above); otherwise the writer's or the file system's error.
 */
Error UILayouts_Save(UILayouts* self);
