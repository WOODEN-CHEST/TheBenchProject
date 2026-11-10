#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "wr/WRUserData.h"
#include "UIDockTree.h"
#include "UIElement.h"
#include "UIMenuBar.h"
#include "UIStatusBar.h"
#include "UIWidget.h"
#include "UIWindow.h"


/**
 * @file UIWorkspace.h
 * @brief The workspace (kind workspace): the root of a screen that uses windows. An optional menu bar along its top and status bar along
 *        its bottom, a dock filling what is left between them, and the floating windows above it; windows dock in it, tab, split, float
 *        again, maximize out of it, and the whole arrangement is kept as named layouts that persist (UILayouts.h).
 *
 * MAKING ONE. UIKit_CreateWorkspace makes it as a root of the screen's Background layer over the whole surface, with its bars when its
 * options ask for them (UIWorkspace_GetMenuBarId, _GetStatusBarId: calling code adds entries and sections), an empty dock, and what shows
 * the drop zones (a root of the Overlay layer). It sets the screen's workspace insets to its bars (UIScreen_SetWorkspaceInsets), so
 * floating and maximized windows use what the bars leave. One workspace per screen.
 *
 * ITS WINDOWS. The workspace keeps the windows opened through it (UIWorkspace_OpenType, a layout) or handed to it (UIWorkspace_AdoptWindow):
 * each floats, is docked, or is maximized out of the dock. A window that closes leaves it.
 *
 * THE DOCK is a split tree of docking areas (UIDockTree.h): each area always shows its tab strip, even with one window; the selected tab's
 * window shows below it, and windows in tabs that are not selected are hidden and get no updates. The selected tab shows its icon and its
 * name, the others their icon only (a placeholder letter, the name's first, without one), the name as hover text. A press on a tab selects
 * it and gives its window the focus as a press on a title bar does (the window's last focus inside, entered again if it was); a double click
 * maximizes it out of the dock. Its context menu: Undock, Maximize, Close. A strip too short for its tabs takes the room from the selected
 * tab's name first; past that the selected tab still shows, with as many before it as fit, and the wheel over a strip steps through its
 * tabs as presses would, so every tab can be reached. Splitters between areas drag to set their split's ratio, each side kept at its
 * windows' least size while there is room. A docked window has no title bar, frame or minimize of its own.
 *
 * DOCKING BY DRAG. A tab dragged along its strip reorders; dragged a few pixels away from the strip, its window leaves the dock and floats
 * under the pointer, carried on by the same press (UIWindow_StartMove). While a window is dragged by its title bar over the dock, the area
 * under the pointer shows its drop guides (UIDockTree_DropZoneAt: its centre's adds a tab, as its tab strip does, and the one beside it
 * toward each edge splits the area there), the one under the pointer lit with where the window would go; letting go on a guide or a strip
 * docks it, and anywhere else it floats where it was let go, so a floating window moves freely over the dock (a release in the very tick a
 * tab was torn out docks nothing: the dock has not been laid out again). Escape puts it back where the drag began: a window torn out of a
 * tab goes back to its tab, the dock as it was, unless the dock changed some other way meanwhile, when the window floats where it was
 * before the drag. A window that does not fit an area's least size is docked anyway: it clips and scrolls.
 *
 * MAXIMIZED OUT OF THE DOCK, a window fills the workspace in the Maximized layer while its tab keeps its place; restored (its button, a
 * double click on its title bar), it goes back to its tab.
 *
 * LAYOUTS (UIWorkspace_SetLayouts): the arrangement (the dock tree and its ratios, each area's tabs and selection, the floating windows'
 * boxes and states, which is maximized, where each window type's last window was) is captured into the active layout, by window type
 * keys, and saved when it has settled; switching layouts closes what the next one lacks and opens what it has, reusing the windows of the
 * same types, whose state stays with them. A host gives the default layout as code that builds it on the live workspace.
 *
 * Main thread only, as every widget.
 */


// Forward declarations.
/** @brief The named layouts; full type in UILayouts.h. */
typedef struct UILayoutsStruct UILayouts;
/** @brief The window types; full type in UIWindowTypes.h. */
typedef struct UIWindowTypesStruct UIWindowTypes;
/** @brief The widget factory; full type in UIWidgetFactory.h. */
typedef struct UIWidgetFactoryStruct UIWidgetFactory;


// Macros.
/** @brief The most windows a workspace keeps: docked, floating and maximized together. */
#define UI_WORKSPACE_MAX_WINDOWS UI_DOCK_TREE_MAX_TABS
/** @brief How far past its tab strip, in the workspace's UI pixels, a tab's drag takes its window out of the dock. */
#define UI_WORKSPACE_UNDOCK_DISTANCE 8.0f


// Types.
/**
 * @brief Where a workspace's window is.
 */
typedef enum UIWorkspacePlaceEnum
{
    /** @brief Above the dock, a root of the Floating layer (or minimized there). */
    UIWorkspacePlace_Floating,
    /** @brief In the dock, as a tab of a docking area. */
    UIWorkspacePlace_Docked,
    /** @brief Maximized out of the dock, its tab keeping its place. */
    UIWorkspacePlace_Maximized
} UIWorkspacePlace;

/**
 * @brief One window a workspace keeps. Read-only outside the module.
 */
typedef struct UIWorkspaceWindowStruct
{
    /** @brief The window. */
    uint64_t WindowId;
    /** @brief Its tab while it has one (docked, or maximized out of the dock); WIDGET_ID_INVALID otherwise. */
    uint64_t TabId;
    /** @brief Where it is. */
    UIWorkspacePlace Place;
} UIWorkspaceWindow;

/**
 * @brief The theme's sizes a workspace's dock is laid out by, in its own UI pixels; its look tells them (UIWorkspaceCapability_SetMetrics).
 */
typedef struct UIWorkspaceMetricsStruct
{
    /** @brief A tab strip's height. */
    float TabHeight;
    /** @brief Inside a tab, at its left and right. */
    float TabPadding;
    /** @brief Between two tabs, and before the first. */
    float TabSpacing;
    /** @brief Between a selected tab's icon and its name. */
    float IconSpacing;
    /** @brief The widest a selected tab gets with its name. */
    float TabMaxWidth;
    /** @brief A splitter's thickness. */
    float SplitterThickness;
    /** @brief A drop guide's side. */
    float GuideSize;
} UIWorkspaceMetrics;

/**
 * @brief What the drop zones show while a window is dragged over a workspace's dock (UIWorkspace_GetDropView), in surface pixels. A plain
 *        value.
 */
typedef struct UIWorkspaceDropViewStruct
{
    /** @brief The docking area under the pointer; the whole dock while it is empty. */
    Rectangle LeafBox;
    /** @brief Its tab strip's height; 0 for an empty dock. */
    float StripHeight;
    /** @brief A guide's side. */
    float GuideSize;
    /** @brief Whether the dock is empty: only the centre's guide shows then. */
    bool IsDockEmpty;
    /** @brief Whether the pointer is on a guide or a tab strip: a release there docks the window. */
    bool HasTarget;
    /** @brief Which zone, while HasTarget. */
    UIDockZone Zone;
    /** @brief Where the window would show, while HasTarget. */
    Rectangle Preview;
} UIWorkspaceDropView;

/**
 * @brief Builds a workspace's default layout on the live workspace: opens window types and docks them through the workspace's functions.
 *        Called when there is no layout to show (the first start, a broken layouts file) and to reset one; the workspace has no windows
 *        then.
 * @param screen The workspace's screen.
 * @param workspaceId The workspace.
 * @param userData What was given with it (its stored copy).
 * @returns Success, or an error: what was opened stays, and the error is logged.
 */
typedef Error (*UIWorkspaceLayoutBuilder)(UIScreen* screen, uint64_t workspaceId, const UserData* userData);

/**
 * @brief What a workspace is created with (UIKit_CreateWorkspace). A plain value; build one with UIWorkspaceOptions_Create.
 */
typedef struct UIWorkspaceOptionsStruct
{
    /** @brief What every element is created with: its variant, look and scale. Its placement and stack hint are not used: a workspace
     *  covers its surface. */
    UIElementOptions Element;
    /** @brief Whether it has a menu bar along its top, made with MenuBar. */
    bool HasMenuBar;
    /** @brief The menu bar's options, when HasMenuBar (their placement is not used). */
    UIMenuBarOptions MenuBar;
    /** @brief Whether it has a status bar along its bottom, made with StatusBar. */
    bool HasStatusBar;
    /** @brief The status bar's options, when HasStatusBar (their placement is not used). */
    UIStatusBarOptions StatusBar;
    /** @brief The window types it opens by key and keeps layouts of; borrowed, it must outlive the workspace. NULL for none. */
    UIWindowTypes* Types;
} UIWorkspaceOptions;

/**
 * @brief What a workspace look's Construct gets (UIKit_CreateWorkspace passes it): the kit and the options.
 */
typedef struct UIWorkspaceArgsStruct
{
    /** @brief The kit; borrowed. */
    UIKit* Kit;
    /** @brief The options; borrowed for the call. */
    const UIWorkspaceOptions* Options;
} UIWorkspaceArgs;

/**
 * @brief A workspace's capability: its windows, its dock, its parts, the drag over it and its layouts, no look. Embedded by a look's
 *        widget type right after its Widget (UIWorkspaceLookBase); its fields are read-only outside the workspace's modules.
 */
typedef struct UIWorkspaceCapabilityStruct
{
    /** @brief The dock: its items are its docked windows' ids. */
    UIDockTree _tree;
    /** @brief The windows it keeps, in the order they came. */
    UIWorkspaceWindow _windows[UI_WORKSPACE_MAX_WINDOWS];
    size_t _windowCount;
    /** @brief Each split's splitter, by the split's node index; WIDGET_ID_INVALID where there is none. */
    uint64_t _splitterIds[UI_DOCK_TREE_MAX_NODES];
    /** @brief Its bars (WIDGET_ID_INVALID without one) and what shows the drop zones. */
    uint64_t _menuBarId;
    uint64_t _statusBarId;
    uint64_t _zonesId;
    /** @brief The dock's arrangement as its last layout made it, in surface pixels, and whether there is one yet; a digest of the dock it
     *  was made from (its splits, ratios and tabs), so a drop is never worked out against an arrangement older than the dock. */
    UIDockArrangement _arrangement;
    bool _hasArrangement;
    uint64_t _arrangedDock;
    /** @brief The dock's area as its last layout put it, in surface pixels. */
    Rectangle _dockArea;
    /** @brief The tab strip's height and a drop guide's side its last layout used, in surface pixels. */
    float _stripPixels;
    float _guidePixels;
    /** @brief The sizes it lays its dock out by. */
    UIWorkspaceMetrics _metrics;
    /** @brief Whether each area's shown window is active (the focus in it), by the area's node index, as its last update saw. */
    bool _isLeafActive[UI_DOCK_TREE_MAX_NODES];
    /** @brief The window being dragged by its title bar over it, or WIDGET_ID_INVALID; whether it was torn out of a tab, the dock as it
     *  was then and the window's floating box then (what Escape puts back). */
    uint64_t _dragWindowId;
    bool _isDragFromTab;
    UIDockTree _dragTree;
    Rectangle _dragFloatingBox;
    /** @brief A digest of the dock right after the tab left: Escape puts _dragTree back only while the dock is still that. */
    uint64_t _dragTornDock;
    /** @brief Whether a tab is handing its press to its window (UIWindow_StartMove), _dragTree and _dragFloatingBox holding what was
     *  before the tab left. */
    bool _isTearingTab;
    /** @brief Where the dragged window is now: whether over the dock (and no other window), the area under the pointer (UI_DOCK_NODE_NONE
     *  over an empty dock or a splitter), whether on a target (a guide or a strip) and which zone. */
    bool _isOverDock;
    int32_t _dropLeaf;
    bool _hasDrop;
    UIDockZone _dropZone;
    /** @brief The window types it opens by key; borrowed, or NULL. */
    UIWindowTypes* _types;
    /** @brief Its layouts; borrowed, or NULL while it has none. */
    UILayouts* _layouts;
    /** @brief Builds the default layout, with its data; NULL for none (an empty workspace). */
    UIWorkspaceLayoutBuilder _defaultLayout;
    UserData _defaultLayoutData;
    /** @brief A digest of the arrangement as last seen: when it changes, the active layout has changed. */
    uint64_t _fingerprint;
    bool _hasFingerprint;
    /** @brief Whether the layouts changed in a way the digest does not see (a name, a new one): saved once settled. */
    bool _isSaveWanted;
    /** @brief Whether keeping the arrangement in the active layout failed and was logged: quiet until it succeeds again. */
    bool _isStoreFailureLogged;
    /** @brief A layout to switch to at the next update (a switcher's choice, posted at the end of an update), and which. */
    bool _hasPendingSwitch;
    size_t _pendingSwitch;
    /** @brief Its layout switcher (a dropdown), kept showing the active layout; WIDGET_ID_INVALID for none. */
    uint64_t _switcherId;
    /** @brief The layouts' revision the switcher last showed. */
    uint64_t _switcherRevision;
    /** @brief Moves on whenever a layout is added, renamed, removed or made active. */
    uint64_t _layoutsRevision;
    /** @brief Whether it shows: hidden, its floating windows and its zones hide with it. */
    bool _isShown;
} UIWorkspaceCapability;

/**
 * @brief How every workspace look's widget struct starts: the widget, then the capability.
 */
typedef struct UIWorkspaceLookBaseStruct
{
    /** @brief The widget. */
    Widget Base;
    /** @brief The workspace's behaviour. */
    UIWorkspaceCapability Workspace;
} UIWorkspaceLookBase;


// Helpers.
/**
 * @brief Default options: a menu bar and a status bar, no window types.
 * @returns The options.
 */
static inline UIWorkspaceOptions UIWorkspaceOptions_Create(void)
{
    UIWorkspaceOptions Options =
    {
        .Element = UIElementOptions_Create(),
        .HasMenuBar = true,
        .MenuBar = UIMenuBarOptions_Create(),
        .HasStatusBar = true,
        .StatusBar = UIStatusBarOptions_Create(),
        .Types = NULL
    };
    return Options;
}


// Calling code: by widget id.
/**
 * @brief Returns whether a widget is a workspace of the kit's.
 * @param screen The screen; must not be NULL.
 * @param widgetId The widget.
 * @returns true for a live workspace.
 */
bool UIWorkspace_IsWorkspace(UIScreen* screen, uint64_t widgetId);

/**
 * @brief Reads a workspace's menu bar.
 * @param screen The workspace's screen; must not be NULL.
 * @param workspaceId The workspace.
 * @param outMenuBarId [out] Receives it, WIDGET_ID_INVALID without one. Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no workspace.
 */
Error UIWorkspace_GetMenuBarId(UIScreen* screen, uint64_t workspaceId, uint64_t* outMenuBarId);

/**
 * @brief Reads a workspace's status bar.
 * @param screen The workspace's screen; must not be NULL.
 * @param workspaceId The workspace.
 * @param outStatusBarId [out] Receives it, WIDGET_ID_INVALID without one. Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no workspace.
 */
Error UIWorkspace_GetStatusBarId(UIScreen* screen, uint64_t workspaceId, uint64_t* outStatusBarId);

/**
 * @brief Shows or hides a workspace with its floating windows and its drop zones: a page of a UI that has others. Hidden, it leaves the
 *        screen's workspace insets as they are and takes no drops.
 * @param screen The workspace's screen; must not be NULL.
 * @param workspaceId The workspace.
 * @param isShown Whether it shows.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no workspace; otherwise a hook's error.
 */
Error UIWorkspace_SetShown(UIScreen* screen, uint64_t workspaceId, bool isShown);

/**
 * @brief Opens a window of a type through a workspace (UIWindowTypes_OpenAt) and keeps it: a single type's window it has already comes
 *        back (its tab selected when docked); a new one floats where its type's last one was.
 * @param screen The workspace's screen; must not be NULL.
 * @param workspaceId The workspace.
 * @param typeIndex The type's index in the workspace's window types.
 * @param outWindowId [out] Receives the window, or WIDGET_ID_INVALID when its fill closed it; may be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id, a widget that is no workspace, one with no window types or full, or a type
 *          that is not active; otherwise the error of opening it.
 */
Error UIWorkspace_OpenType(UIScreen* screen, uint64_t workspaceId, uint32_t typeIndex, uint64_t* outWindowId);

/**
 * @brief Opens a window of a type by its key through a workspace (UIWorkspace_OpenType).
 * @param screen The workspace's screen; must not be NULL.
 * @param workspaceId The workspace.
 * @param key The type's key; must not be NULL.
 * @param outWindowId [out] Receives the window; may be NULL.
 * @returns As UIWorkspace_OpenType; ErrorCode_InvalidOperation for a key no active type has.
 */
Error UIWorkspace_OpenTypeByKey(UIScreen* screen, uint64_t workspaceId, const unsigned char* key, uint64_t* outWindowId);

/**
 * @brief Hands a floating window made some other way to a workspace, which keeps it from then on.
 * @param screen The workspace's screen; must not be NULL.
 * @param workspaceId The workspace.
 * @param windowId A window that is a root of the screen, not maximized and not another workspace's.
 * @returns Success (also for one it keeps already); ErrorCode_InvalidOperation for a stale id, a widget that is no workspace or no
 *          window, a window that is not a floating root, or a full workspace.
 */
Error UIWorkspace_AdoptWindow(UIScreen* screen, uint64_t workspaceId, uint64_t windowId);

/**
 * @brief Docks a window the workspace keeps: as a tab beside another docked window (UIDockZone_Center: after it in its area), or at one of
 *        that window's area's sides (a new area); in an empty dock, as its first area whatever the zone. Its tab is selected.
 * @param screen The workspace's screen; must not be NULL.
 * @param workspaceId The workspace.
 * @param windowId A window the workspace keeps, floating or docked (a docked one moves).
 * @param besideWindowId A docked window of the workspace, or WIDGET_ID_INVALID for an empty dock.
 * @param zone Where beside it.
 * @returns Success; ErrorCode_InvalidOperation for a stale id, a widget that is no workspace, a window it does not keep, a neighbour that
 *          is not docked (or none while the dock has areas), or a dock as full as its limits allow (UIDockTree.h); otherwise a hook's error.
 */
Error UIWorkspace_Dock(UIScreen* screen, uint64_t workspaceId, uint64_t windowId, uint64_t besideWindowId, UIDockZone zone);

/**
 * @brief Takes a window out of the dock: it floats at its floating box, in front.
 * @param screen The workspace's screen; must not be NULL.
 * @param workspaceId The workspace.
 * @param windowId A docked window of the workspace (or one maximized out of the dock, which is restored first).
 * @returns Success; ErrorCode_InvalidOperation for a stale id, a widget that is no workspace, or a window it does not keep docked;
 *          otherwise a hook's error.
 */
Error UIWorkspace_Undock(UIScreen* screen, uint64_t workspaceId, uint64_t windowId);

/**
 * @brief Maximizes a docked window out of the dock: it fills the workspace, its tab keeping its place, and goes back to it when restored.
 * @param screen The workspace's screen; must not be NULL.
 * @param workspaceId The workspace.
 * @param windowId A docked window of the workspace.
 * @returns Success; ErrorCode_InvalidOperation for a stale id, a widget that is no workspace, or a window it does not keep docked;
 *          otherwise a hook's error.
 */
Error UIWorkspace_MaximizeDocked(UIScreen* screen, uint64_t workspaceId, uint64_t windowId);

/**
 * @brief Sets how much of the room of the split above a docked window's area that area takes: what a default layout builds with.
 * @param screen The workspace's screen; must not be NULL.
 * @param workspaceId The workspace.
 * @param windowId A docked window of the workspace whose area is one side of a split.
 * @param share The area's share, finite, kept within [0, 1].
 * @returns Success; ErrorCode_InvalidOperation for a stale id, a widget that is no workspace, a window it does not keep docked, or one whose
 *          area is the whole dock; ErrorCode_ArgumentOutOfRange for a share that is not finite.
 */
Error UIWorkspace_SetAreaShare(UIScreen* screen, uint64_t workspaceId, uint64_t windowId, float share);

/**
 * @brief Selects a docked window's tab: it shows in its area, the one shown there before hides.
 * @param screen The workspace's screen; must not be NULL.
 * @param workspaceId The workspace.
 * @param windowId A docked window of the workspace.
 * @returns Success; ErrorCode_InvalidOperation for a stale id, a widget that is no workspace, or a window it does not keep docked;
 *          otherwise a hook's error.
 */
Error UIWorkspace_SelectTab(UIScreen* screen, uint64_t workspaceId, uint64_t windowId);

/**
 * @brief Reads where a window of a workspace is.
 * @param screen The workspace's screen; must not be NULL.
 * @param workspaceId The workspace.
 * @param windowId The window.
 * @param outPlace [out] Receives where it is. Must not be NULL.
 * @returns true when the workspace keeps the window.
 */
bool UIWorkspace_GetPlace(UIScreen* screen, uint64_t workspaceId, uint64_t windowId, UIWorkspacePlace* outPlace);

/**
 * @brief Reads a workspace's dock: its tree, whose items are its docked windows' ids.
 * @param screen The workspace's screen; must not be NULL.
 * @param workspaceId The workspace.
 * @returns The tree, valid until the workspace changes; NULL for a stale id or a widget that is no workspace.
 */
const UIDockTree* UIWorkspace_GetTree(UIScreen* screen, uint64_t workspaceId);

/**
 * @brief Reads one of a workspace's windows' tab.
 * @param screen The workspace's screen; must not be NULL.
 * @param workspaceId The workspace.
 * @param windowId The window.
 * @returns Its tab's id; WIDGET_ID_INVALID for a window with no tab or a workspace that does not keep it.
 */
uint64_t UIWorkspace_GetTabId(UIScreen* screen, uint64_t workspaceId, uint64_t windowId);

/**
 * @brief Reads one of a workspace's splitters.
 * @param screen The workspace's screen; must not be NULL.
 * @param workspaceId The workspace.
 * @param split The split's node index in its tree.
 * @returns The splitter's id; WIDGET_ID_INVALID for an index that is no split.
 */
uint64_t UIWorkspace_GetSplitterId(UIScreen* screen, uint64_t workspaceId, int32_t split);

/**
 * @brief Reads what the drop zones show now, while a window is dragged by its title bar over a workspace's dock: the area under the pointer
 *        with its guides and, on a guide or a strip, where the window would go.
 * @param screen The workspace's screen; must not be NULL.
 * @param workspaceId The workspace.
 * @param outView [out] Receives it. Must not be NULL.
 * @returns true while zones show: a window of the workspace is dragged over an area of its dock (or over its empty dock).
 */
bool UIWorkspace_GetDropView(UIScreen* screen, uint64_t workspaceId, UIWorkspaceDropView* outView);


// Layouts.
/**
 * @brief Gives a workspace its layouts and its default, and shows the active layout (or, with none, builds the default and keeps it as a
 *        layout named "Default"). From then on the arrangement is kept in the active layout once its changes settle
 *        (UI_LAYOUTS_SETTLE_SECONDS: the file is saved then), when another layout is shown or made, and when the host saves.
 * @param screen The workspace's screen; must not be NULL.
 * @param workspaceId The workspace.
 * @param layouts The layouts, loaded already (UILayouts_Load); borrowed, it must outlive the workspace or be taken away first (NULL). Its
 *        window types must be the workspace's.
 * @param makeDefault Builds the default layout; NULL for an empty one.
 * @param userData Handed to @p makeDefault; copied. May be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id, a widget that is no workspace, or one with no window types; otherwise the
 *          error of showing the layout, or of building the default (what it built is kept as "Default" either way).
 */
Error UIWorkspace_SetLayouts(UIScreen* screen, uint64_t workspaceId, UILayouts* layouts, UIWorkspaceLayoutBuilder makeDefault, const UserData* userData);

/**
 * @brief Switches a workspace to another of its layouts: the arrangement is kept in the active one, then the other's is shown (windows the
 *        new one lacks closed, those it has opened, windows of the same types reused). An arrangement the layout left cannot keep (logged
 *        once) leaves it as it was, and the switch goes on.
 * @param screen The workspace's screen; must not be NULL.
 * @param workspaceId The workspace.
 * @param index The layout's index.
 * @returns Success (nothing for the active one); ErrorCode_InvalidOperation for a stale id or a workspace with no layouts;
 *          ErrorCode_ArgumentOutOfRange for an index it does not have; otherwise the error of showing it.
 */
Error UIWorkspace_SwitchLayout(UIScreen* screen, uint64_t workspaceId, size_t index);

/**
 * @brief Keeps the arrangement as a new layout and makes it the active one; the layout that was active keeps it too, as a switch away from it
 *        would.
 * @param screen The workspace's screen; must not be NULL.
 * @param workspaceId The workspace.
 * @param name Its name: 1 to UI_LAYOUT_MAX_NAME_BYTES bytes of UTF-8 without control characters, not another layout's; NULL for one
 *        made up ("Layout", or "Layout 2" and on while that is taken).
 * @param outIndex [out] Receives its index; may be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id, a workspace with no layouts, or as many layouts as there may be;
 *          ErrorCode_ArgumentOutOfRange for a name that is not valid or taken; otherwise the error of keeping the arrangement.
 */
Error UIWorkspace_NewLayout(UIScreen* screen, uint64_t workspaceId, const unsigned char* name, size_t* outIndex);

/**
 * @brief Renames a layout.
 * @param screen The workspace's screen; must not be NULL.
 * @param workspaceId The workspace.
 * @param index The layout's index.
 * @param name Its new name (as UIWorkspace_NewLayout's).
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a workspace with no layouts; ErrorCode_ArgumentOutOfRange for an index it
 *          does not have or a name that is not valid or taken.
 */
Error UIWorkspace_RenameLayout(UIScreen* screen, uint64_t workspaceId, size_t index, const unsigned char* name);

/**
 * @brief Deletes a layout; deleting the active one switches to the one before it (or after, for the first).
 * @param screen The workspace's screen; must not be NULL.
 * @param workspaceId The workspace.
 * @param index The layout's index.
 * @returns Success; ErrorCode_InvalidOperation for a stale id, a workspace with no layouts, or its last layout; ErrorCode_ArgumentOutOfRange
 *          for an index it does not have; otherwise the error of switching.
 */
Error UIWorkspace_DeleteLayout(UIScreen* screen, uint64_t workspaceId, size_t index);

/**
 * @brief Puts the active layout back to the default: every window the workspace keeps is closed (asking each's close query; one that
 *        stays floats, out of the dock), then the default is built on the empty dock and kept in the active layout (what the builder
 *        made, also when it failed).
 * @param screen The workspace's screen; must not be NULL.
 * @param workspaceId The workspace.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a workspace with no layouts; otherwise the builder's or a hook's error.
 */
Error UIWorkspace_ResetLayout(UIScreen* screen, uint64_t workspaceId);

/**
 * @brief Keeps the arrangement in the active layout and saves the layouts now: what a host calls before it goes (the workspace's own
 *        teardown cannot, its windows going first).
 * @param screen The workspace's screen; must not be NULL.
 * @param workspaceId The workspace.
 * @returns Success (also with no layouts); ErrorCode_InvalidOperation for a stale id or a widget that is no workspace; otherwise the error
 *          of keeping the arrangement (the file is written with the layouts as they were) or of the save, logged either way.
 */
Error UIWorkspace_SaveLayouts(UIScreen* screen, uint64_t workspaceId);

/**
 * @brief Makes a layout switcher: a dropdown listing a workspace's layouts by name, the active one chosen; choosing one switches to it at
 *        the workspace's next update.
 * @param kit The kit; must not be NULL.
 * @param screen The screen; must not be NULL.
 * @param parentId Where it goes (a menu bar's or a status bar's section); an alive widget of the screen.
 * @param workspaceId The workspace.
 * @param outId [out] Receives the dropdown, or WIDGET_ID_INVALID on a failure (nothing stays behind). Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL; ErrorCode_InvalidOperation for a workspace that is gone; otherwise the
 *          error of making it.
 */
Error UIWorkspace_CreateLayoutSwitcher(UIKit* kit, UIScreen* screen, uint64_t parentId, uint64_t workspaceId, uint64_t* outId);


// Behaviour helpers, for looks and the kit.
/**
 * @brief Tells the behaviour the theme's sizes it lays the dock out by; a look calls it from its Update. Values that are not finite numbers
 *        >= 0 count as 0; the tab strip is at least UI_WINDOW_MIN_TITLE_HEIGHT high, so there is always a tab to press.
 * @param self The capability; must not be NULL.
 * @param widget The workspace's widget; must not be NULL.
 * @param metrics The sizes; must not be NULL.
 */
void UIWorkspaceCapability_SetMetrics(UIWorkspaceCapability* self, Widget* widget, const UIWorkspaceMetrics* metrics);

/**
 * @brief Reads one docking area's boxes for a look to draw, as the last layout put them, in surface pixels.
 * @param self The capability; must not be NULL.
 * @param leaf The area's node index.
 * @param outStrip [out] Receives its tab strip's box. Must not be NULL.
 * @param outIsActive [out] Receives whether its shown window is active (the focus in it). Must not be NULL.
 * @returns true for an area of the dock.
 */
bool UIWorkspaceCapability_GetLeaf(const UIWorkspaceCapability* self, int32_t leaf, Rectangle* outStrip, bool* outIsActive);

/**
 * @brief Registers the kit's own widget types a workspace is made of, a tab, a splitter and what shows the drop zones, into the kit's
 *        records. UIKit_Construct calls it.
 * @param kit The kit; must not be NULL.
 * @param factory The kit's factory; must not be NULL.
 * @returns Success, or the factory's error.
 */
Error UIWorkspace_RegisterTypes(UIKit* kit, UIWidgetFactory* factory);
