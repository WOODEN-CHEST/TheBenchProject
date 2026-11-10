#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "wr/WRUserData.h"
// Rectangle and Vector2 are the boxes and points of this public API.
#include "raylib/raylib.h"


/**
 * @file UIDockTree.h
 * @brief The docking areas of a workspace as pure data: a tree whose inner nodes split an area in two and whose leaves hold tabs. No
 *        widgets, no GL: everything here is arithmetic over plain values, headless.
 *
 * NODES. A split divides its box across (its first child at the left, its second at the right) or down (first above, second below),
 * its first child taking a share of the room (its ratio, a fraction kept as the user left it). A leaf is a docking area: its tabs, in
 * order, each an item the caller chooses (a window's widget id while the game runs, an index into a layout's windows in a file), and
 * the selected one. Items are never UI_DOCK_ITEM_NONE and appear once in a tree. Nodes are addressed by index; an index is stable
 * until the node is removed, and a removed node's index is used again by the next node made.
 *
 * OPERATIONS. Adding, removing, moving and selecting tabs, splitting a leaf to put an item at one of its sides, setting a split's
 * ratio. An operation that would break a limit (UI_DOCK_TREE_MAX_LEAVES, UI_DOCK_TREE_MAX_TABS, UI_DOCK_TREE_MAX_DEPTH) or that names
 * a node or an item it cannot refuses with an error and changes nothing. Removing tabs leaves empty leaves in place, so a remove and an
 * add make one move; UIDockTree_CollapseEmpty then takes empty leaves away, each split left with one child giving its place to it.
 *
 * ARRANGING (UIDockTree_Arrange) gives every node its box in an area, in whole units (the caller's pixels): a split keeps each side at
 * least at its minimum (from the caller's function, per leaf; a split's is its children's together) while there is room for both, and
 * below that gives both sides the same share of their minimums. Neighbours share their edges exactly: no gaps, no overlaps. DROP ZONES
 * (UIDockTree_DropZoneAt) say where a point over the arrangement would put a window: a leaf's tab strip and its centre guide add a tab,
 * the guides beside the centre's split it at that edge, and anywhere else the window floats.
 *
 * A tree is a plain value (no pointers inside), so it can be copied and kept whole: a drag keeps the tree it began with and puts it
 * back on Escape.
 */


// Macros.
/** @brief The most leaves a tree has. */
#define UI_DOCK_TREE_MAX_LEAVES 32
/** @brief The most nodes a tree has: its leaves and the splits between them. */
#define UI_DOCK_TREE_MAX_NODES (2 * UI_DOCK_TREE_MAX_LEAVES - 1)
/** @brief The most tabs a tree holds, all its leaves together. */
#define UI_DOCK_TREE_MAX_TABS 128
/** @brief The most splits on the way from the root to a leaf. */
#define UI_DOCK_TREE_MAX_DEPTH 16
/** @brief No node: an empty tree's root, a root's parent. */
#define UI_DOCK_NODE_NONE ((int32_t)-1)
/** @brief No item: a leaf with no tabs has this selected. Never an item of a tree. */
#define UI_DOCK_ITEM_NONE ((uint64_t)0)
/** @brief The ratio a split made by splitting a leaf starts with: half each. */
#define UI_DOCK_TREE_DEFAULT_RATIO 0.5f


// Types.
/**
 * @brief What a node of a tree is.
 */
typedef enum UIDockNodeKindEnum
{
    /** @brief Not in use. */
    UIDockNodeKind_Free,
    /** @brief A docking area holding tabs. */
    UIDockNodeKind_Leaf,
    /** @brief Two nodes side by side or one above the other. */
    UIDockNodeKind_Split
} UIDockNodeKind;

/**
 * @brief Which way a split divides its box.
 */
typedef enum UIDockSplitEnum
{
    /** @brief Side by side: its first child at the left, its second at the right, a splitter between them dragged across. */
    UIDockSplit_Across,
    /** @brief One above the other: its first child at the top, its second at the bottom, a splitter between them dragged down. */
    UIDockSplit_Down
} UIDockSplit;

/**
 * @brief Where in a leaf a dropped window goes.
 */
typedef enum UIDockZoneEnum
{
    /** @brief Its centre (or its tab strip): a tab of the leaf. */
    UIDockZone_Center,
    /** @brief Its left edge: a leaf of its own at the leaf's left. */
    UIDockZone_Left,
    /** @brief Its top edge: a leaf of its own above the leaf. */
    UIDockZone_Top,
    /** @brief Its right edge: a leaf of its own at the leaf's right. */
    UIDockZone_Right,
    /** @brief Its bottom edge: a leaf of its own below the leaf. */
    UIDockZone_Bottom,
    /** @brief The number of zones. Not one. */
    UIDockZone_Count
} UIDockZone;

/**
 * @brief One node of a tree. Read-only outside the module: read it through UIDockTree_GetNode.
 */
typedef struct UIDockNodeStruct
{
    /** @brief What it is. */
    UIDockNodeKind Kind;
    /** @brief Its parent's index, or UI_DOCK_NODE_NONE for the root. */
    int32_t Parent;
    /** @brief For a split: which way it divides. */
    UIDockSplit Split;
    /** @brief For a split: its first child's share of the room, in [0, 1]. */
    float Ratio;
    /** @brief For a split: its first and second child's indices. */
    int32_t Children[2];
    /** @brief For a leaf: its selected tab's item, or UI_DOCK_ITEM_NONE while it has no tabs. */
    uint64_t Selected;
} UIDockNode;

/**
 * @brief One tab of a tree: its item and the leaf holding it. Internal to the module.
 */
typedef struct UIDockTabStruct
{
    /** @brief The item. */
    uint64_t Item;
    /** @brief The leaf's index. */
    int32_t Leaf;
} UIDockTab;

/**
 * @brief A tree: nodes and tabs in fixed arrays, so it is a plain value with no allocations. Construct with UIDockTree_Construct; its
 *        fields are read-only outside the module.
 */
typedef struct UIDockTreeStruct
{
    /** @brief The nodes, by index. */
    UIDockNode _nodes[UI_DOCK_TREE_MAX_NODES];
    /** @brief Every tab in order: a leaf's tabs are those naming it, in this order. */
    UIDockTab _tabs[UI_DOCK_TREE_MAX_TABS];
    /** @brief How many of _tabs are used. */
    size_t _tabCount;
    /** @brief The root's index, or UI_DOCK_NODE_NONE for an empty tree. */
    int32_t _root;
} UIDockTree;

/**
 * @brief Gives the least size a leaf needs, in the arrangement's units, its tab strip included. Called during UIDockTree_Arrange, once
 *        per leaf; it must not change the tree.
 * @param tree The tree being arranged.
 * @param leaf The leaf's index.
 * @param userData What was given to UIDockTree_Arrange.
 * @returns The size; a component that is not a finite number >= 0 counts as 0.
 */
typedef Vector2 (*UIDockMinimumFunction)(const UIDockTree* tree, int32_t leaf, const UserData* userData);

/**
 * @brief Where every node of a tree lies in an area (UIDockTree_Arrange), in whole units. A plain value.
 */
typedef struct UIDockArrangementStruct
{
    /** @brief The area arranged, its edges rounded to whole units. */
    Rectangle Area;
    /** @brief The splitters' thickness, a whole number of units. */
    float SplitterThickness;
    /** @brief Each node's box by index: a leaf's whole box (its tab strip and what it shows), a split's whole box. Empty for a free node. */
    Rectangle NodeBoxes[UI_DOCK_TREE_MAX_NODES];
    /** @brief Each split's splitter, between its children. Empty for a node that is no split. */
    Rectangle SplitterBoxes[UI_DOCK_TREE_MAX_NODES];
    /** @brief The least size each node needs: a leaf's from the function, a split's its children's together with the splitter. */
    Vector2 Minimums[UI_DOCK_TREE_MAX_NODES];
} UIDockArrangement;


// Lifecycle.
/**
 * @brief Makes an empty tree: no nodes, no tabs.
 * @param self The tree; must not be NULL.
 */
void UIDockTree_Construct(UIDockTree* self);

/**
 * @brief Releases a tree. It holds nothing to release; present so the hook exists.
 * @param self The tree, or NULL.
 */
void UIDockTree_Deconstruct(UIDockTree* self);


// Reading.
/**
 * @brief Returns whether a tree has no nodes.
 * @param self The tree; must not be NULL.
 * @returns true for an empty tree.
 */
static inline bool UIDockTree_IsEmpty(const UIDockTree* self)
{
    return self->_root == UI_DOCK_NODE_NONE;
}

/**
 * @brief Returns a tree's root.
 * @param self The tree; must not be NULL.
 * @returns The root's index, or UI_DOCK_NODE_NONE for an empty tree.
 */
static inline int32_t UIDockTree_GetRoot(const UIDockTree* self)
{
    return self->_root;
}

/**
 * @brief Returns how many tabs a tree holds, all its leaves together.
 * @param self The tree; must not be NULL.
 * @returns The count.
 */
static inline size_t UIDockTree_GetTotalTabCount(const UIDockTree* self)
{
    return self->_tabCount;
}

/**
 * @brief Reads a node.
 * @param self The tree; must not be NULL.
 * @param index The node's index.
 * @returns The node, valid until the tree changes; NULL for an index out of range or a free node.
 */
const UIDockNode* UIDockTree_GetNode(const UIDockTree* self, int32_t index);

/**
 * @brief Returns whether an index names a leaf of the tree.
 * @param self The tree; must not be NULL.
 * @param index The node's index.
 * @returns true for a leaf.
 */
bool UIDockTree_IsLeaf(const UIDockTree* self, int32_t index);

/**
 * @brief Returns whether an index names a split of the tree.
 * @param self The tree; must not be NULL.
 * @param index The node's index.
 * @returns true for a split.
 */
bool UIDockTree_IsSplit(const UIDockTree* self, int32_t index);

/**
 * @brief Returns how many leaves a tree has.
 * @param self The tree; must not be NULL.
 * @returns The count.
 */
size_t UIDockTree_GetLeafCount(const UIDockTree* self);

/**
 * @brief Returns how many tabs a leaf holds.
 * @param self The tree; must not be NULL.
 * @param leaf The leaf's index.
 * @returns The count; 0 for an index that is no leaf.
 */
size_t UIDockTree_GetTabCount(const UIDockTree* self, int32_t leaf);

/**
 * @brief Returns one of a leaf's tabs, in order.
 * @param self The tree; must not be NULL.
 * @param leaf The leaf's index.
 * @param index The tab's place in the leaf.
 * @returns Its item; UI_DOCK_ITEM_NONE for a leaf or a place the tree does not have.
 */
uint64_t UIDockTree_GetTabAt(const UIDockTree* self, int32_t leaf, size_t index);

/**
 * @brief Reads one of a tree's tabs by its place among all of them (for walking every tab).
 * @param self The tree; must not be NULL.
 * @param index Its place, below UIDockTree_GetTotalTabCount.
 * @param outItem [out] Receives its item; may be NULL.
 * @param outLeaf [out] Receives its leaf; may be NULL.
 * @returns true for a place the tree has.
 */
bool UIDockTree_GetTab(const UIDockTree* self, size_t index, uint64_t* outItem, int32_t* outLeaf);

/**
 * @brief Returns a leaf's selected tab.
 * @param self The tree; must not be NULL.
 * @param leaf The leaf's index.
 * @returns Its item; UI_DOCK_ITEM_NONE for a leaf with no tabs or an index that is no leaf.
 */
uint64_t UIDockTree_GetSelected(const UIDockTree* self, int32_t leaf);

/**
 * @brief Finds an item's tab.
 * @param self The tree; must not be NULL.
 * @param item The item.
 * @param outLeaf [out] Receives its leaf's index; may be NULL.
 * @param outIndex [out] Receives its place in the leaf; may be NULL.
 * @returns true when the tree holds the item.
 */
bool UIDockTree_FindTab(const UIDockTree* self, uint64_t item, int32_t* outLeaf, size_t* outIndex);

/**
 * @brief Returns how many splits lie on the way from the root to a node.
 * @param self The tree; must not be NULL.
 * @param index The node's index.
 * @returns The depth: 0 for the root; 0 for an index that is not in use.
 */
size_t UIDockTree_GetDepth(const UIDockTree* self, int32_t index);

/**
 * @brief Checks a tree whole: every node in use reached once from the root and its parent link agreeing, two children per split, finite
 *        ratios in [0, 1], the limits kept, every tab's leaf a leaf, items present once and never UI_DOCK_ITEM_NONE, and each leaf's
 *        selected tab one of its own (none only while it is empty). Empty leaves are allowed (UIDockTree_CollapseEmpty takes them away).
 * @param self The tree; must not be NULL.
 * @returns true for a whole tree.
 */
bool UIDockTree_IsValid(const UIDockTree* self);

/**
 * @brief Returns whether a tree has a leaf with no tabs.
 * @param self The tree; must not be NULL.
 * @returns true when one is empty.
 */
bool UIDockTree_HasEmptyLeaf(const UIDockTree* self);


// Changing.
/**
 * @brief Adds a tab to a leaf and selects it. On an empty tree, with @p leaf UI_DOCK_NODE_NONE, it makes the root: a leaf holding it.
 * @param self The tree; must not be NULL.
 * @param leaf The leaf's index; UI_DOCK_NODE_NONE only for an empty tree.
 * @param item The item: not UI_DOCK_ITEM_NONE, not in the tree.
 * @param index Its place among the leaf's tabs; past the last, it goes last.
 * @returns Success; ErrorCode_ArgumentOutOfRange for an item that is none or in the tree, or a leaf the tree does not have;
 *          ErrorCode_InvalidOperation when the tree holds UI_DOCK_TREE_MAX_TABS tabs.
 */
Error UIDockTree_AddTab(UIDockTree* self, int32_t leaf, uint64_t item, size_t index);

/**
 * @brief Takes an item's tab away. When it was its leaf's selected one, the tab after it is selected, else the one before, else none.
 *        The leaf stays, empty or not.
 * @param self The tree; must not be NULL.
 * @param item The item.
 * @returns Success; ErrorCode_ArgumentOutOfRange for an item the tree does not hold.
 */
Error UIDockTree_RemoveTab(UIDockTree* self, uint64_t item);

/**
 * @brief Moves an item's tab to a place in a leaf (its own or another) and selects it there; the leaf it left selects as after
 *        UIDockTree_RemoveTab, and stays even when empty.
 * @param self The tree; must not be NULL.
 * @param item The item, in the tree.
 * @param leaf The leaf it goes to.
 * @param index Its place among that leaf's tabs once it has left its own; past the last, it goes last.
 * @returns Success; ErrorCode_ArgumentOutOfRange for an item the tree does not hold or a leaf it does not have.
 */
Error UIDockTree_MoveTab(UIDockTree* self, uint64_t item, int32_t leaf, size_t index);

/**
 * @brief Puts an item at one side of a leaf: the leaf's place goes to a split (across for the left and right, down for the top and
 *        bottom, at UI_DOCK_TREE_DEFAULT_RATIO) holding the leaf and a new leaf whose one tab is the item, on that side.
 * @param self The tree; must not be NULL.
 * @param leaf The leaf's index.
 * @param side The side: UIDockZone_Left, _Top, _Right or _Bottom.
 * @param item The item: not UI_DOCK_ITEM_NONE, not in the tree.
 * @param outNewLeaf [out] Receives the new leaf's index; may be NULL.
 * @returns Success; ErrorCode_ArgumentOutOfRange for a leaf the tree does not have, a side that is no edge, or an item that is none or in
 *          the tree; ErrorCode_InvalidOperation when a limit would be passed (leaves, tabs, depth).
 */
Error UIDockTree_SplitLeaf(UIDockTree* self, int32_t leaf, UIDockZone side, uint64_t item, int32_t* outNewLeaf);

/**
 * @brief Takes every leaf with no tabs away: a split left with one child gives its place to that child, and a root left with nothing makes
 *        the tree empty.
 * @param self The tree; must not be NULL.
 */
void UIDockTree_CollapseEmpty(UIDockTree* self);

/**
 * @brief Sets a split's ratio: its first child's share of the room.
 * @param self The tree; must not be NULL.
 * @param split The split's index.
 * @param ratio The share; finite, kept within [0, 1].
 * @returns Success; ErrorCode_ArgumentOutOfRange for an index that is no split or a ratio that is not finite.
 */
Error UIDockTree_SetRatio(UIDockTree* self, int32_t split, float ratio);

/**
 * @brief Selects an item's tab in its leaf.
 * @param self The tree; must not be NULL.
 * @param item The item.
 * @returns Success; ErrorCode_ArgumentOutOfRange for an item the tree does not hold.
 */
Error UIDockTree_SelectTab(UIDockTree* self, uint64_t item);

/**
 * @brief Builds a tree node by node from its root down, as a reader of a stored tree does: makes a split (with its ratio) or an empty leaf,
 *        as the root of an empty tree or as a child still missing of a split made before. The tree is whole once every split has both
 *        children (UIDockTree_IsValid); tabs go in with UIDockTree_AddTab.
 * @param self The tree; must not be NULL.
 * @param parent A split of the tree missing the child at @p place, or UI_DOCK_NODE_NONE for the root of an empty tree.
 * @param place 0 for the split's first child, 1 for its second.
 * @param kind UIDockNodeKind_Leaf or UIDockNodeKind_Split.
 * @param split For a split: which way it divides.
 * @param ratio For a split: its first child's share; finite, kept within [0, 1].
 * @param outIndex [out] Receives the node's index; may be NULL.
 * @returns Success; ErrorCode_ArgumentOutOfRange for a parent, place, kind or direction it cannot take, or a ratio that is not finite;
 *          ErrorCode_InvalidOperation when the tree has as many nodes, leaves or levels as it can.
 */
Error UIDockTree_BuildNode(UIDockTree* self, int32_t parent, int32_t place, UIDockNodeKind kind, UIDockSplit split, float ratio,
    int32_t* outIndex);

/**
 * @brief Puts another item in an item's place: its tab, its leaf's selection.
 * @param self The tree; must not be NULL.
 * @param item The item in the tree.
 * @param newItem The item in its place: not UI_DOCK_ITEM_NONE, not in the tree (or @p item itself).
 * @returns Success; ErrorCode_ArgumentOutOfRange for an item the tree does not hold or a new item that is none or in the tree.
 */
Error UIDockTree_ReplaceItem(UIDockTree* self, uint64_t item, uint64_t newItem);


// Arranging.
/**
 * @brief Gives every node of a tree its box in an area, in whole units (above, ARRANGING).
 * @param self The tree; must not be NULL.
 * @param area The area; its edges are rounded to whole units, and a size below 0 counts as 0.
 * @param splitterThickness The splitters' thickness; rounded to a whole number >= 0.
 * @param minimum Gives each leaf's least size; NULL for none.
 * @param userData Handed to @p minimum; may be NULL.
 * @param outArrangement [out] Receives the boxes. Must not be NULL.
 */
void UIDockTree_Arrange(const UIDockTree* self, Rectangle area, float splitterThickness, UIDockMinimumFunction minimum, const UserData* userData,
    UIDockArrangement* outArrangement);

/**
 * @brief Returns the ratio that puts a split's splitter under a point (its middle on it), kept so each side keeps its minimum while
 *        there is room for both: what dragging a splitter sets.
 * @param self The tree; must not be NULL.
 * @param arrangement The tree's arrangement; must not be NULL.
 * @param split The split's index.
 * @param point The point, in the arrangement's units.
 * @returns The ratio in [0, 1]; the split's own ratio when it has no room to share or the index is no split.
 */
float UIDockTree_GetRatioAt(const UIDockTree* self, const UIDockArrangement* arrangement, int32_t split, Vector2 point);

/**
 * @brief Finds where a point over an arrangement would put a dropped window: the leaf under it and the zone, when the point lies on the
 *        leaf's tab strip (its top @p stripHeight: the centre, a tab) or on one of its five guides (UIDockZone_GetGuide: the centre's, or
 *        one of the four edges' beside it). Anywhere else on a leaf a window floats, so a floating window can be moved over the dock. Over
 *        an empty tree, the area's centre guide is the one zone (the leaf is UI_DOCK_NODE_NONE then).
 * @param self The tree; must not be NULL.
 * @param arrangement The tree's arrangement; must not be NULL.
 * @param point The point, in the arrangement's units.
 * @param stripHeight How much of a leaf's top is its tab strip.
 * @param guideSize A guide's side, in the arrangement's units.
 * @param outLeaf [out] Receives the leaf's index (UI_DOCK_NODE_NONE over an empty tree). Must not be NULL.
 * @param outZone [out] Receives the zone. Must not be NULL.
 * @returns true when the point lies on a strip or a guide; false anywhere else.
 */
bool UIDockTree_DropZoneAt(const UIDockTree* self, const UIDockArrangement* arrangement, Vector2 point, float stripHeight, float guideSize,
    int32_t* outLeaf, UIDockZone* outZone);

/**
 * @brief Returns one of a leaf's guides: a square of @p guideSize in the middle of what the leaf shows below its strip for the centre, and
 *        one beside it on each side for the edges (a sixth of a guide apart), all kept inside the leaf's box as far as they fit: below
 *        its strip while there is room there, over the strip in a leaf too short for that.
 * @param leafBox The leaf's box.
 * @param stripHeight How much of its top is its tab strip.
 * @param zone The zone.
 * @param guideSize A guide's side.
 * @returns The guide's box, in whole units.
 */
Rectangle UIDockZone_GetGuide(Rectangle leafBox, float stripHeight, UIDockZone zone, float guideSize);

/**
 * @brief Returns where a window dropped in a zone of a leaf would show: the whole leaf for the centre, the side a split of the default
 *        ratio would give it for an edge.
 * @param leafBox The leaf's box.
 * @param zone The zone.
 * @param splitterThickness The splitters' thickness.
 * @returns The box, in whole units.
 */
Rectangle UIDockZone_GetPreview(Rectangle leafBox, UIDockZone zone, float splitterThickness);
