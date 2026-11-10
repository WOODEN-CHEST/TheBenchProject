#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "UIKit.h"
#include "UIKitInternal.h"
#include "UIWorkspace.h"


/*
 * Private to the workspace's modules: UIWorkspace.c (the kind: its windows, its dock, docking and the drag over it, laying it out),
 * UIWorkspaceParts.c (the kit's own widget types it is made of: a tab, a splitter, what shows the drop zones) and UIWorkspaceLayouts.c
 * (capturing and showing layouts, saving them, the switcher).
 */


// Types.
/** What a tab is made with: its workspace and its window. */
typedef struct UIDockTabArgsStruct
{
    UIKit* Kit;
    uint64_t WorkspaceId;
    uint64_t WindowId;
} UIDockTabArgs;

/** What a splitter is made with: its workspace and its split's node index. */
typedef struct UIDockSplitterArgsStruct
{
    uint64_t WorkspaceId;
    int32_t Split;
} UIDockSplitterArgs;


// The workspace (UIWorkspace.c).
/** The workspace's capability, or NULL for a widget that is gone or no workspace. */
UIWorkspaceCapability* UIWorkspace_InternalFind(UIScreen* screen, uint64_t workspaceId, Widget** outWidget);

/** One of its windows' record, or NULL. */
UIWorkspaceWindow* UIWorkspace_InternalFindWindow(UIWorkspaceCapability* self, uint64_t windowId);

/** Makes its widgets follow its records and its dock: windows gone leave, each window where its place says (docked in it with a tab and
 *  shown while selected, maximized out of it, floating), restored ones back to their tabs, a splitter per split. */
Error UIWorkspace_InternalReconcile(UIScreen* screen, uint64_t workspaceId);

/** A press on a docked window's tab: the tab selected, its window shown and given the focus as a title bar's press gives it. */
Error UIWorkspace_InternalPressTab(UIScreen* screen, uint64_t workspaceId, uint64_t windowId);

/** A left press on a tab moved: along its strip it reorders; a few pixels away from the strip, its window leaves the dock and the press
 *  goes on carrying it (*outIsTorn true then). */
Error UIWorkspace_InternalDragTab(UIScreen* screen, uint64_t workspaceId, uint64_t windowId, GameKey button, Vector2 screenPosition, bool* outIsTorn);

/** A splitter's press moved: its split's ratio follows the pointer. */
Error UIWorkspace_InternalDragSplitter(UIScreen* screen, uint64_t workspaceId, int32_t split, Vector2 screenPosition);

/** Opens a docked window's tab's context menu (Undock, Maximize, Close). */
Error UIWorkspace_InternalOpenTabMenu(UIScreen* screen, const UIContextMenuRequest* request, uint64_t workspaceId, uint64_t windowId);

/** Maximizes a docked window out of the dock, or puts one maximized out of it back. */
Error UIWorkspace_InternalToggleMaximized(UIScreen* screen, uint64_t workspaceId, uint64_t windowId);

/** Opens a window of a type, as UIWorkspace_OpenType, without asking for a single type's window it has (a layout opening what it holds). */
Error UIWorkspace_InternalOpenNew(UIScreen* screen, uint64_t workspaceId, uint32_t typeIndex, uint64_t* outWindowId);

/** Gives the workspace its parts once: its bars and what shows the drop zones. */
Error UIWorkspace_InternalSetParts(UIScreen* screen, uint64_t workspaceId, uint64_t menuBarId, uint64_t statusBarId, uint64_t zonesId);


// The parts (UIWorkspaceParts.c).
/** Registers the tab's, the splitter's and the zones' widget types into the kit's records. */
Error UIWorkspace_InternalRegisterParts(UIKit* kit, UIWidgetFactory* factory);

/** Makes a docked window's tab as a child of the workspace, with its hover text and its context menu. */
Error UIWorkspace_InternalCreateTab(UIScreen* screen, uint64_t workspaceId, uint64_t windowId, uint64_t* outTabId);

/** Makes a split's splitter as a child of the workspace. */
Error UIWorkspace_InternalCreateSplitter(UIScreen* screen, uint64_t workspaceId, int32_t split, uint64_t* outSplitterId);

/** Makes what shows the drop zones: a root of the Overlay layer over the whole surface. */
Error UIWorkspace_InternalCreateZones(UIScreen* screen, uint64_t workspaceId, uint64_t* outZonesId);


// The layouts (UIWorkspaceLayouts.c).
/** Its part of the workspace's update: a pending switch, the arrangement's digest noted as a change, the layouts saved once settled. */
Error UIWorkspace_InternalUpdateLayouts(UIScreen* screen, uint64_t workspaceId, double timeSeconds);

/** A digest of the arrangement a layout keeps: when it changes, the active layout has changed. */
uint64_t UIWorkspace_InternalFingerprint(UIScreen* screen, const UIWorkspaceCapability* self);

/* A digest of what a dock's arrangement is made from: its splits, their ratios, and which windows each area holds (not which is selected). */
uint64_t UIWorkspace_InternalDockDigest(const UIDockTree* tree);
