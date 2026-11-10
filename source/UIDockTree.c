#include "UIDockTree.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"


// Macros.
/** No place in the tabs' array. */
#define TAB_POSITION_NONE ((size_t)-1)
/** The largest minimum a leaf is taken to need, so the sums of every node's stay finite. */
#define MAX_MINIMUM 1e9f


// Static functions: helpers.
static bool IsFiniteFloat(float value)
{
    return !Math_IsNaNFloat(value) && !Math_IsInfinityFloat(value);
}

static float RoundToUnit(float value)
{
    return Math_FloorFloat(value + 0.5f);
}

/* A length that is a finite number >= 0, else 0. */
static float CleanLength(float value)
{
    return (IsFiniteFloat(value) && (value > 0.0f)) ? value : 0.0f;
}

static bool IsInUse(const UIDockTree* self, int32_t index)
{
    return (index >= 0) && (index < (int32_t)UI_DOCK_TREE_MAX_NODES) && (self->_nodes[index].Kind != UIDockNodeKind_Free);
}

static void FreeNode(UIDockTree* self, int32_t index)
{
    self->_nodes[index] = (UIDockNode){ .Kind = UIDockNodeKind_Free, .Parent = UI_DOCK_NODE_NONE, .Split = UIDockSplit_Across, .Ratio = 0.0f,
        .Children = { UI_DOCK_NODE_NONE, UI_DOCK_NODE_NONE }, .Selected = UI_DOCK_ITEM_NONE };
}

/* The first free node after @p after, or UI_DOCK_NODE_NONE. */
static int32_t FindFreeNode(const UIDockTree* self, int32_t after)
{
    for (int32_t Index = after + 1; Index < (int32_t)UI_DOCK_TREE_MAX_NODES; Index++)
    {
        if (self->_nodes[Index].Kind == UIDockNodeKind_Free)
        {
            return Index;
        }
    }
    return UI_DOCK_NODE_NONE;
}

static size_t FindTabPosition(const UIDockTree* self, uint64_t item)
{
    for (size_t Index = 0U; Index < self->_tabCount; Index++)
    {
        if (self->_tabs[Index].Item == item)
        {
            return Index;
        }
    }
    return TAB_POSITION_NONE;
}

/* Where a leaf's tab at a place lies in the tabs' array, or TAB_POSITION_NONE. */
static size_t FindLeafTabPosition(const UIDockTree* self, int32_t leaf, size_t place)
{
    size_t Seen = 0U;
    for (size_t Index = 0U; Index < self->_tabCount; Index++)
    {
        if (self->_tabs[Index].Leaf != leaf)
        {
            continue;
        }
        if (Seen == place)
        {
            return Index;
        }
        Seen++;
    }
    return TAB_POSITION_NONE;
}

/* Where a tab of a leaf goes in the tabs' array to be at a place among the leaf's tabs: before its tab now there, else at the end (the
 * array's order across leaves means nothing). */
static size_t FindInsertPosition(const UIDockTree* self, int32_t leaf, size_t place)
{
    size_t AtPlace = FindLeafTabPosition(self, leaf, place);
    return (AtPlace != TAB_POSITION_NONE) ? AtPlace : self->_tabCount;
}

static void InsertTabAt(UIDockTree* self, size_t position, uint64_t item, int32_t leaf)
{
    for (size_t Index = self->_tabCount; Index > position; Index--)
    {
        self->_tabs[Index] = self->_tabs[Index - 1U];
    }
    self->_tabs[position] = (UIDockTab){ .Item = item, .Leaf = leaf };
    self->_tabCount++;
}

static void RemoveTabAt(UIDockTree* self, size_t position)
{
    for (size_t Index = position; (Index + 1U) < self->_tabCount; Index++)
    {
        self->_tabs[Index] = self->_tabs[Index + 1U];
    }
    self->_tabCount--;
}

/* A tab's place among its leaf's tabs. */
static size_t GetPlaceInLeaf(const UIDockTree* self, size_t position)
{
    size_t Place = 0U;
    for (size_t Index = 0U; Index < position; Index++)
    {
        if (self->_tabs[Index].Leaf == self->_tabs[position].Leaf)
        {
            Place++;
        }
    }
    return Place;
}

/* A leaf's selection after its tab at a place, the selected one, went: the tab now there, else the one before, else none. */
static void SelectAfterRemoval(UIDockTree* self, int32_t leaf, size_t place)
{
    size_t Count = UIDockTree_GetTabCount(self, leaf);
    if (Count == 0U)
    {
        self->_nodes[leaf].Selected = UI_DOCK_ITEM_NONE;
        return;
    }
    size_t Chosen = (place < Count) ? place : (Count - 1U);
    self->_nodes[leaf].Selected = UIDockTree_GetTabAt(self, leaf, Chosen);
}

/* Takes the tab at a position out of the array, its leaf choosing another selection if it was the selected one. */
static void TakeOutTab(UIDockTree* self, size_t position)
{
    int32_t Leaf = self->_tabs[position].Leaf;
    uint64_t Item = self->_tabs[position].Item;
    size_t Place = GetPlaceInLeaf(self, position);
    RemoveTabAt(self, position);
    if (self->_nodes[Leaf].Selected == Item)
    {
        SelectAfterRemoval(self, Leaf, Place);
    }
}

static bool IsItemFree(const UIDockTree* self, uint64_t item)
{
    return (item != UI_DOCK_ITEM_NONE) && (FindTabPosition(self, item) == TAB_POSITION_NONE);
}

static bool IsEdge(UIDockZone zone)
{
    return (zone == UIDockZone_Left) || (zone == UIDockZone_Top) || (zone == UIDockZone_Right) || (zone == UIDockZone_Bottom);
}

/* One node's place in its parent's children, replaced by another node (or the root's, when it has no parent). */
static void ReplaceChild(UIDockTree* self, int32_t parent, int32_t oldChild, int32_t newChild)
{
    if (parent == UI_DOCK_NODE_NONE)
    {
        self->_root = newChild;
        return;
    }
    UIDockNode* Parent = &self->_nodes[parent];
    Parent->Children[(Parent->Children[0] == oldChild) ? 0 : 1] = newChild;
}

static int32_t FindEmptyLeaf(const UIDockTree* self)
{
    for (int32_t Index = 0; Index < (int32_t)UI_DOCK_TREE_MAX_NODES; Index++)
    {
        if ((self->_nodes[Index].Kind == UIDockNodeKind_Leaf) && (UIDockTree_GetTabCount(self, Index) == 0U))
        {
            return Index;
        }
    }
    return UI_DOCK_NODE_NONE;
}


// Static functions: checking.
/* Walks the tree from the root: every node reached once, its parent link agreeing, two distinct children per split, finite ratios in
 * [0, 1], the depth kept. Counts the leaves. */
static bool CheckStructure(const UIDockTree* self, bool* visited, size_t* outLeafCount)
{
    int32_t Stack[UI_DOCK_TREE_MAX_NODES];
    size_t Depths[UI_DOCK_TREE_MAX_NODES];
    size_t Count = 0U;
    Stack[Count] = self->_root;
    Depths[Count] = 0U;
    Count++;
    *outLeafCount = 0U;
    while (Count > 0U)
    {
        Count--;
        int32_t Index = Stack[Count];
        size_t Depth = Depths[Count];
        if (!IsInUse(self, Index) || visited[Index] || (Depth > UI_DOCK_TREE_MAX_DEPTH))
        {
            return false;
        }
        visited[Index] = true;
        const UIDockNode* Node = &self->_nodes[Index];
        if (Node->Kind == UIDockNodeKind_Leaf)
        {
            (*outLeafCount)++;
            continue;
        }
        bool IsRatioValid = IsFiniteFloat(Node->Ratio) && (Node->Ratio >= 0.0f) && (Node->Ratio <= 1.0f);
        bool IsSplitValid = (Node->Split == UIDockSplit_Across) || (Node->Split == UIDockSplit_Down);
        if (!IsRatioValid || !IsSplitValid || (Node->Children[0] == Node->Children[1]))
        {
            return false;
        }
        for (int32_t Child = 0; Child < 2; Child++)
        {
            int32_t ChildIndex = Node->Children[Child];
            if (!IsInUse(self, ChildIndex) || (self->_nodes[ChildIndex].Parent != Index) || (Count >= UI_DOCK_TREE_MAX_NODES))
            {
                return false;
            }
            Stack[Count] = ChildIndex;
            Depths[Count] = Depth + 1U;
            Count++;
        }
    }
    return true;
}

static bool CheckTabs(const UIDockTree* self, const bool* visited)
{
    if (self->_tabCount > UI_DOCK_TREE_MAX_TABS)
    {
        return false;
    }
    for (size_t Index = 0U; Index < self->_tabCount; Index++)
    {
        const UIDockTab* Tab = &self->_tabs[Index];
        if ((Tab->Item == UI_DOCK_ITEM_NONE) || (Tab->Leaf < 0) || (Tab->Leaf >= (int32_t)UI_DOCK_TREE_MAX_NODES) || !visited[Tab->Leaf]
            || (self->_nodes[Tab->Leaf].Kind != UIDockNodeKind_Leaf))
        {
            return false;
        }
        for (size_t Other = Index + 1U; Other < self->_tabCount; Other++)
        {
            if (self->_tabs[Other].Item == Tab->Item)
            {
                return false;
            }
        }
    }
    for (int32_t Index = 0; Index < (int32_t)UI_DOCK_TREE_MAX_NODES; Index++)
    {
        const UIDockNode* Node = &self->_nodes[Index];
        if (Node->Kind != UIDockNodeKind_Leaf)
        {
            continue;
        }
        bool HasTabs = UIDockTree_GetTabCount(self, Index) > 0U;
        int32_t SelectedLeaf = UI_DOCK_NODE_NONE;
        bool IsSelectedOwn = UIDockTree_FindTab(self, Node->Selected, &SelectedLeaf, NULL) && (SelectedLeaf == Index);
        if (HasTabs ? !IsSelectedOwn : (Node->Selected != UI_DOCK_ITEM_NONE))
        {
            return false;
        }
    }
    return true;
}


// Static functions: arranging.
static Vector2 ComputeMinimums(const UIDockTree* self, int32_t index, size_t depth, float splitter, UIDockMinimumFunction minimum,
    const UserData* userData, UIDockArrangement* out)
{
    Vector2 Result = { .x = 0.0f, .y = 0.0f };
    if (!IsInUse(self, index) || (depth > UI_DOCK_TREE_MAX_DEPTH))
    {
        return Result;
    }
    const UIDockNode* Node = &self->_nodes[index];
    if (Node->Kind == UIDockNodeKind_Leaf)
    {
        Vector2 Asked = (minimum != NULL) ? minimum(self, index, userData) : Result;
        Result = (Vector2){ .x = Math_MinFloat(Math_CeilFloat(CleanLength(Asked.x)), MAX_MINIMUM),
            .y = Math_MinFloat(Math_CeilFloat(CleanLength(Asked.y)), MAX_MINIMUM) };
    }
    else
    {
        Vector2 First = ComputeMinimums(self, Node->Children[0], depth + 1U, splitter, minimum, userData, out);
        Vector2 Second = ComputeMinimums(self, Node->Children[1], depth + 1U, splitter, minimum, userData, out);
        bool IsAcross = Node->Split == UIDockSplit_Across;
        Result.x = IsAcross ? (First.x + splitter + Second.x) : Math_MaxFloat(First.x, Second.x);
        Result.y = IsAcross ? Math_MaxFloat(First.y, Second.y) : (First.y + splitter + Second.y);
    }
    out->Minimums[index] = Result;
    return Result;
}

/* The first child's length of a split's room: its ratio's share, kept so both keep their minimums while there is room for both, else a
 * share of the room by their minimums. Whole units, within [0, inner]. */
static float GetFirstLength(float inner, float ratio, float firstMinimum, float secondMinimum)
{
    float First = RoundToUnit(inner * ratio);
    if ((firstMinimum + secondMinimum) <= inner)
    {
        First = Math_ClampFloat(First, firstMinimum, inner - secondMinimum);
    }
    else if ((firstMinimum + secondMinimum) > 0.0f)
    {
        First = RoundToUnit(inner * (firstMinimum / (firstMinimum + secondMinimum)));
    }
    return Math_ClampFloat(First, 0.0f, Math_MaxFloat(0.0f, inner));
}

static void AssignBoxes(const UIDockTree* self, int32_t index, size_t depth, Rectangle box, UIDockArrangement* out)
{
    if (!IsInUse(self, index) || (depth > UI_DOCK_TREE_MAX_DEPTH))
    {
        return;
    }
    out->NodeBoxes[index] = box;
    const UIDockNode* Node = &self->_nodes[index];
    if (Node->Kind != UIDockNodeKind_Split)
    {
        return;
    }
    bool IsAcross = Node->Split == UIDockSplit_Across;
    float Total = IsAcross ? box.width : box.height;
    float Splitter = Math_MinFloat(out->SplitterThickness, Total);
    float Inner = Total - Splitter;
    int32_t FirstChild = Node->Children[0];
    int32_t SecondChild = Node->Children[1];
    float FirstMinimum = IsAcross ? out->Minimums[FirstChild].x : out->Minimums[FirstChild].y;
    float SecondMinimum = IsAcross ? out->Minimums[SecondChild].x : out->Minimums[SecondChild].y;
    float First = GetFirstLength(Inner, Node->Ratio, FirstMinimum, SecondMinimum);
    Rectangle FirstBox = box;
    Rectangle SplitterBox = box;
    Rectangle SecondBox = box;
    if (IsAcross)
    {
        FirstBox.width = First;
        SplitterBox.x = box.x + First;
        SplitterBox.width = Splitter;
        SecondBox.x = box.x + First + Splitter;
        SecondBox.width = Inner - First;
    }
    else
    {
        FirstBox.height = First;
        SplitterBox.y = box.y + First;
        SplitterBox.height = Splitter;
        SecondBox.y = box.y + First + Splitter;
        SecondBox.height = Inner - First;
    }
    out->SplitterBoxes[index] = SplitterBox;
    AssignBoxes(self, FirstChild, depth + 1U, FirstBox, out);
    AssignBoxes(self, SecondChild, depth + 1U, SecondBox, out);
}

static bool Contains(Rectangle box, Vector2 point)
{
    return (point.x >= box.x) && (point.y >= box.y) && (point.x < (box.x + box.width)) && (point.y < (box.y + box.height));
}


// Public functions: lifecycle.
void UIDockTree_Construct(UIDockTree* self)
{
    for (int32_t Index = 0; Index < (int32_t)UI_DOCK_TREE_MAX_NODES; Index++)
    {
        FreeNode(self, Index);
    }
    Memory_Zero(self->_tabs, sizeof(self->_tabs));
    self->_tabCount = 0U;
    self->_root = UI_DOCK_NODE_NONE;
}

void UIDockTree_Deconstruct(UIDockTree* self)
{
    (void)self;
}


// Public functions: reading.
const UIDockNode* UIDockTree_GetNode(const UIDockTree* self, int32_t index)
{
    return IsInUse(self, index) ? &self->_nodes[index] : NULL;
}

bool UIDockTree_IsLeaf(const UIDockTree* self, int32_t index)
{
    return IsInUse(self, index) && (self->_nodes[index].Kind == UIDockNodeKind_Leaf);
}

bool UIDockTree_IsSplit(const UIDockTree* self, int32_t index)
{
    return IsInUse(self, index) && (self->_nodes[index].Kind == UIDockNodeKind_Split);
}

size_t UIDockTree_GetLeafCount(const UIDockTree* self)
{
    size_t Count = 0U;
    for (int32_t Index = 0; Index < (int32_t)UI_DOCK_TREE_MAX_NODES; Index++)
    {
        Count += (self->_nodes[Index].Kind == UIDockNodeKind_Leaf) ? 1U : 0U;
    }
    return Count;
}

size_t UIDockTree_GetTabCount(const UIDockTree* self, int32_t leaf)
{
    if (!UIDockTree_IsLeaf(self, leaf))
    {
        return 0U;
    }
    size_t Count = 0U;
    for (size_t Index = 0U; Index < self->_tabCount; Index++)
    {
        Count += (self->_tabs[Index].Leaf == leaf) ? 1U : 0U;
    }
    return Count;
}

uint64_t UIDockTree_GetTabAt(const UIDockTree* self, int32_t leaf, size_t index)
{
    if (!UIDockTree_IsLeaf(self, leaf))
    {
        return UI_DOCK_ITEM_NONE;
    }
    size_t Position = FindLeafTabPosition(self, leaf, index);
    return (Position != TAB_POSITION_NONE) ? self->_tabs[Position].Item : UI_DOCK_ITEM_NONE;
}

bool UIDockTree_GetTab(const UIDockTree* self, size_t index, uint64_t* outItem, int32_t* outLeaf)
{
    if (index >= self->_tabCount)
    {
        return false;
    }
    if (outItem != NULL)
    {
        *outItem = self->_tabs[index].Item;
    }
    if (outLeaf != NULL)
    {
        *outLeaf = self->_tabs[index].Leaf;
    }
    return true;
}

uint64_t UIDockTree_GetSelected(const UIDockTree* self, int32_t leaf)
{
    return UIDockTree_IsLeaf(self, leaf) ? self->_nodes[leaf].Selected : UI_DOCK_ITEM_NONE;
}

bool UIDockTree_FindTab(const UIDockTree* self, uint64_t item, int32_t* outLeaf, size_t* outIndex)
{
    size_t Position = (item != UI_DOCK_ITEM_NONE) ? FindTabPosition(self, item) : TAB_POSITION_NONE;
    if (Position == TAB_POSITION_NONE)
    {
        return false;
    }
    if (outLeaf != NULL)
    {
        *outLeaf = self->_tabs[Position].Leaf;
    }
    if (outIndex != NULL)
    {
        *outIndex = GetPlaceInLeaf(self, Position);
    }
    return true;
}

size_t UIDockTree_GetDepth(const UIDockTree* self, int32_t index)
{
    size_t Depth = 0U;
    int32_t Current = IsInUse(self, index) ? self->_nodes[index].Parent : UI_DOCK_NODE_NONE;
    while ((Current != UI_DOCK_NODE_NONE) && (Depth < (size_t)UI_DOCK_TREE_MAX_NODES))
    {
        Depth++;
        Current = IsInUse(self, Current) ? self->_nodes[Current].Parent : UI_DOCK_NODE_NONE;
    }
    return Depth;
}

bool UIDockTree_IsValid(const UIDockTree* self)
{
    bool Visited[UI_DOCK_TREE_MAX_NODES] = { false };
    if (self->_root == UI_DOCK_NODE_NONE)
    {
        for (int32_t Index = 0; Index < (int32_t)UI_DOCK_TREE_MAX_NODES; Index++)
        {
            if (self->_nodes[Index].Kind != UIDockNodeKind_Free)
            {
                return false;
            }
        }
        return self->_tabCount == 0U;
    }
    size_t LeafCount = 0U;
    if (!CheckStructure(self, Visited, &LeafCount) || (LeafCount > UI_DOCK_TREE_MAX_LEAVES))
    {
        return false;
    }
    for (int32_t Index = 0; Index < (int32_t)UI_DOCK_TREE_MAX_NODES; Index++)
    {
        if ((self->_nodes[Index].Kind != UIDockNodeKind_Free) && !Visited[Index])
        {
            return false;
        }
    }
    return (self->_nodes[self->_root].Parent == UI_DOCK_NODE_NONE) && CheckTabs(self, Visited);
}

bool UIDockTree_HasEmptyLeaf(const UIDockTree* self)
{
    return FindEmptyLeaf(self) != UI_DOCK_NODE_NONE;
}


// Public functions: changing.
Error UIDockTree_AddTab(UIDockTree* self, int32_t leaf, uint64_t item, size_t index)
{
    if (!IsItemFree(self, item))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIDockTree_AddTab: the item is none, or the tree holds it already.");
    }
    if (self->_tabCount >= UI_DOCK_TREE_MAX_TABS)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIDockTree_AddTab: the tree holds as many tabs as it can.");
    }
    if (UIDockTree_IsEmpty(self) && (leaf == UI_DOCK_NODE_NONE))
    {
        int32_t Root = FindFreeNode(self, UI_DOCK_NODE_NONE);
        FreeNode(self, Root);
        self->_nodes[Root].Kind = UIDockNodeKind_Leaf;
        self->_root = Root;
        leaf = Root;
    }
    if (!UIDockTree_IsLeaf(self, leaf))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIDockTree_AddTab: no such leaf (UI_DOCK_NODE_NONE only makes an empty tree's root).");
    }
    InsertTabAt(self, FindInsertPosition(self, leaf, index), item, leaf);
    self->_nodes[leaf].Selected = item;
    return Error_CreateSuccess();
}

Error UIDockTree_RemoveTab(UIDockTree* self, uint64_t item)
{
    size_t Position = (item != UI_DOCK_ITEM_NONE) ? FindTabPosition(self, item) : TAB_POSITION_NONE;
    if (Position == TAB_POSITION_NONE)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIDockTree_RemoveTab: the tree does not hold the item.");
    }
    TakeOutTab(self, Position);
    return Error_CreateSuccess();
}

Error UIDockTree_MoveTab(UIDockTree* self, uint64_t item, int32_t leaf, size_t index)
{
    size_t Position = (item != UI_DOCK_ITEM_NONE) ? FindTabPosition(self, item) : TAB_POSITION_NONE;
    if ((Position == TAB_POSITION_NONE) || !UIDockTree_IsLeaf(self, leaf))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIDockTree_MoveTab: the tree does not hold the item, or has no such leaf.");
    }
    TakeOutTab(self, Position);
    InsertTabAt(self, FindInsertPosition(self, leaf, index), item, leaf);
    self->_nodes[leaf].Selected = item;
    return Error_CreateSuccess();
}

Error UIDockTree_SplitLeaf(UIDockTree* self, int32_t leaf, UIDockZone side, uint64_t item, int32_t* outNewLeaf)
{
    if (!UIDockTree_IsLeaf(self, leaf) || !IsEdge(side) || !IsItemFree(self, item))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "UIDockTree_SplitLeaf: the leaf must be one of the tree's, the side an edge, the item not none and not in the tree.");
    }
    int32_t SplitIndex = FindFreeNode(self, UI_DOCK_NODE_NONE);
    int32_t NewLeaf = (SplitIndex != UI_DOCK_NODE_NONE) ? FindFreeNode(self, SplitIndex) : UI_DOCK_NODE_NONE;
    bool IsWithinLimits = (NewLeaf != UI_DOCK_NODE_NONE) && (self->_tabCount < UI_DOCK_TREE_MAX_TABS)
        && (UIDockTree_GetLeafCount(self) < UI_DOCK_TREE_MAX_LEAVES) && ((UIDockTree_GetDepth(self, leaf) + 1U) <= UI_DOCK_TREE_MAX_DEPTH);
    if (!IsWithinLimits)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIDockTree_SplitLeaf: the tree has as many leaves, tabs or levels as it can.");
    }
    bool IsNewFirst = (side == UIDockZone_Left) || (side == UIDockZone_Top);
    int32_t Parent = self->_nodes[leaf].Parent;
    FreeNode(self, SplitIndex);
    UIDockNode* Split = &self->_nodes[SplitIndex];
    Split->Kind = UIDockNodeKind_Split;
    Split->Parent = Parent;
    Split->Split = ((side == UIDockZone_Left) || (side == UIDockZone_Right)) ? UIDockSplit_Across : UIDockSplit_Down;
    Split->Ratio = UI_DOCK_TREE_DEFAULT_RATIO;
    Split->Children[0] = IsNewFirst ? NewLeaf : leaf;
    Split->Children[1] = IsNewFirst ? leaf : NewLeaf;
    ReplaceChild(self, Parent, leaf, SplitIndex);
    self->_nodes[leaf].Parent = SplitIndex;
    FreeNode(self, NewLeaf);
    self->_nodes[NewLeaf].Kind = UIDockNodeKind_Leaf;
    self->_nodes[NewLeaf].Parent = SplitIndex;
    self->_nodes[NewLeaf].Selected = item;
    InsertTabAt(self, self->_tabCount, item, NewLeaf);
    if (outNewLeaf != NULL)
    {
        *outNewLeaf = NewLeaf;
    }
    return Error_CreateSuccess();
}

void UIDockTree_CollapseEmpty(UIDockTree* self)
{
    for (size_t Pass = 0U; Pass < (size_t)UI_DOCK_TREE_MAX_NODES; Pass++)
    {
        int32_t Leaf = FindEmptyLeaf(self);
        if (Leaf == UI_DOCK_NODE_NONE)
        {
            return;
        }
        int32_t Parent = self->_nodes[Leaf].Parent;
        if (Parent == UI_DOCK_NODE_NONE)
        {
            FreeNode(self, Leaf);
            self->_root = (self->_root == Leaf) ? UI_DOCK_NODE_NONE : self->_root;
            continue;
        }
        UIDockNode* Split = &self->_nodes[Parent];
        int32_t Sibling = (Split->Children[0] == Leaf) ? Split->Children[1] : Split->Children[0];
        int32_t GrandParent = Split->Parent;
        if (IsInUse(self, Sibling))
        {
            self->_nodes[Sibling].Parent = GrandParent;
        }
        ReplaceChild(self, GrandParent, Parent, Sibling);
        FreeNode(self, Parent);
        FreeNode(self, Leaf);
    }
}

Error UIDockTree_SetRatio(UIDockTree* self, int32_t split, float ratio)
{
    if (!UIDockTree_IsSplit(self, split) || !IsFiniteFloat(ratio))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIDockTree_SetRatio: the index must name a split, the ratio be finite.");
    }
    self->_nodes[split].Ratio = Math_ClampFloat(ratio, 0.0f, 1.0f);
    return Error_CreateSuccess();
}

Error UIDockTree_SelectTab(UIDockTree* self, uint64_t item)
{
    int32_t Leaf = UI_DOCK_NODE_NONE;
    if (!UIDockTree_FindTab(self, item, &Leaf, NULL))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIDockTree_SelectTab: the tree does not hold the item.");
    }
    self->_nodes[Leaf].Selected = item;
    return Error_CreateSuccess();
}

Error UIDockTree_BuildNode(UIDockTree* self, int32_t parent, int32_t place, UIDockNodeKind kind, UIDockSplit split, float ratio,
    int32_t* outIndex)
{
    bool IsRoot = (parent == UI_DOCK_NODE_NONE) && UIDockTree_IsEmpty(self);
    bool IsChild = UIDockTree_IsSplit(self, parent) && ((place == 0) || (place == 1)) && (self->_nodes[parent].Children[place] == UI_DOCK_NODE_NONE);
    bool IsKindValid = (kind == UIDockNodeKind_Leaf) || ((kind == UIDockNodeKind_Split) && ((split == UIDockSplit_Across) || (split == UIDockSplit_Down))
        && IsFiniteFloat(ratio));
    if ((!IsRoot && !IsChild) || !IsKindValid)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "UIDockTree_BuildNode: the parent must be a split missing that child (or none for an empty tree's root), the kind a leaf or a split.");
    }
    int32_t Index = FindFreeNode(self, UI_DOCK_NODE_NONE);
    size_t Depth = IsRoot ? 0U : (UIDockTree_GetDepth(self, parent) + 1U);
    bool IsWithinLimits = (Index != UI_DOCK_NODE_NONE) && (Depth <= UI_DOCK_TREE_MAX_DEPTH)
        && ((kind != UIDockNodeKind_Leaf) || (UIDockTree_GetLeafCount(self) < UI_DOCK_TREE_MAX_LEAVES));
    if (!IsWithinLimits)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIDockTree_BuildNode: the tree has as many nodes, leaves or levels as it can.");
    }
    FreeNode(self, Index);
    UIDockNode* Node = &self->_nodes[Index];
    Node->Kind = kind;
    Node->Parent = parent;
    if (kind == UIDockNodeKind_Split)
    {
        Node->Split = split;
        Node->Ratio = Math_ClampFloat(ratio, 0.0f, 1.0f);
    }
    if (IsRoot)
    {
        self->_root = Index;
    }
    else
    {
        self->_nodes[parent].Children[place] = Index;
    }
    if (outIndex != NULL)
    {
        *outIndex = Index;
    }
    return Error_CreateSuccess();
}

Error UIDockTree_ReplaceItem(UIDockTree* self, uint64_t item, uint64_t newItem)
{
    size_t Position = (item != UI_DOCK_ITEM_NONE) ? FindTabPosition(self, item) : TAB_POSITION_NONE;
    if ((Position == TAB_POSITION_NONE) || ((newItem != item) && !IsItemFree(self, newItem)))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIDockTree_ReplaceItem: the tree must hold the item and not the new one.");
    }
    int32_t Leaf = self->_tabs[Position].Leaf;
    self->_tabs[Position].Item = newItem;
    if (self->_nodes[Leaf].Selected == item)
    {
        self->_nodes[Leaf].Selected = newItem;
    }
    return Error_CreateSuccess();
}


// Public functions: arranging.
void UIDockTree_Arrange(const UIDockTree* self, Rectangle area, float splitterThickness, UIDockMinimumFunction minimum, const UserData* userData,
    UIDockArrangement* outArrangement)
{
    Memory_Zero(outArrangement, sizeof(*outArrangement));
    float Left = IsFiniteFloat(area.x) ? RoundToUnit(area.x) : 0.0f;
    float Top = IsFiniteFloat(area.y) ? RoundToUnit(area.y) : 0.0f;
    float Right = IsFiniteFloat(area.x + area.width) ? RoundToUnit(area.x + area.width) : Left;
    float Bottom = IsFiniteFloat(area.y + area.height) ? RoundToUnit(area.y + area.height) : Top;
    outArrangement->Area = (Rectangle){ .x = Left, .y = Top, .width = Math_MaxFloat(0.0f, Right - Left), .height = Math_MaxFloat(0.0f, Bottom - Top) };
    outArrangement->SplitterThickness = RoundToUnit(CleanLength(splitterThickness));
    if (UIDockTree_IsEmpty(self))
    {
        return;
    }
    (void)ComputeMinimums(self, self->_root, 0U, outArrangement->SplitterThickness, minimum, userData, outArrangement);
    AssignBoxes(self, self->_root, 0U, outArrangement->Area, outArrangement);
}

float UIDockTree_GetRatioAt(const UIDockTree* self, const UIDockArrangement* arrangement, int32_t split, Vector2 point)
{
    if (!UIDockTree_IsSplit(self, split))
    {
        return UI_DOCK_TREE_DEFAULT_RATIO;
    }
    const UIDockNode* Node = &self->_nodes[split];
    bool IsAcross = Node->Split == UIDockSplit_Across;
    Rectangle Box = arrangement->NodeBoxes[split];
    float Total = IsAcross ? Box.width : Box.height;
    float Splitter = Math_MinFloat(arrangement->SplitterThickness, Total);
    float Inner = Total - Splitter;
    float Along = IsAcross ? (point.x - Box.x) : (point.y - Box.y);
    if (!(Inner > 0.0f) || !IsFiniteFloat(Along))
    {
        return Node->Ratio;
    }
    float FirstMinimum = IsAcross ? arrangement->Minimums[Node->Children[0]].x : arrangement->Minimums[Node->Children[0]].y;
    float SecondMinimum = IsAcross ? arrangement->Minimums[Node->Children[1]].x : arrangement->Minimums[Node->Children[1]].y;
    float First = Along - (Splitter * 0.5f);
    if ((FirstMinimum + SecondMinimum) <= Inner)
    {
        First = Math_ClampFloat(First, FirstMinimum, Inner - SecondMinimum);
    }
    else
    {
        return Node->Ratio; // No room for both: the ratio does not change what shows.
    }
    return Math_ClampFloat(First / Inner, 0.0f, 1.0f);
}

bool UIDockTree_DropZoneAt(const UIDockTree* self, const UIDockArrangement* arrangement, Vector2 point, float stripHeight, float guideSize,
    int32_t* outLeaf, UIDockZone* outZone)
{
    *outLeaf = UI_DOCK_NODE_NONE;
    *outZone = UIDockZone_Center;
    if (!IsFiniteFloat(point.x) || !IsFiniteFloat(point.y))
    {
        return false;
    }
    if (UIDockTree_IsEmpty(self))
    {
        return Contains(UIDockZone_GetGuide(arrangement->Area, 0.0f, UIDockZone_Center, guideSize), point);
    }
    for (int32_t Index = 0; Index < (int32_t)UI_DOCK_TREE_MAX_NODES; Index++)
    {
        Rectangle Box = arrangement->NodeBoxes[Index];
        if ((self->_nodes[Index].Kind != UIDockNodeKind_Leaf) || !Contains(Box, point))
        {
            continue;
        }
        *outLeaf = Index;
        if ((point.y - Box.y) < CleanLength(stripHeight))
        {
            return true;
        }
        // The centre's guide first: where guides overlap in a small leaf, a tab wins.
        for (int32_t Zone = 0; Zone < (int32_t)UIDockZone_Count; Zone++)
        {
            if (Contains(UIDockZone_GetGuide(Box, stripHeight, (UIDockZone)Zone, guideSize), point))
            {
                *outZone = (UIDockZone)Zone;
                return true;
            }
        }
        return false;
    }
    return false;
}

Rectangle UIDockZone_GetGuide(Rectangle leafBox, float stripHeight, UIDockZone zone, float guideSize)
{
    float Size = RoundToUnit(CleanLength(guideSize));
    float Gap = RoundToUnit(Size / 6.0f);
    float Strip = Math_MinFloat(CleanLength(stripHeight), CleanLength(leafBox.height));
    Rectangle Content = { .x = leafBox.x, .y = leafBox.y + Strip, .width = CleanLength(leafBox.width), .height = CleanLength(leafBox.height) - Strip };
    float X = Content.x + RoundToUnit((Content.width - Size) * 0.5f);
    float Y = Content.y + RoundToUnit((Content.height - Size) * 0.5f);
    switch (zone)
    {
        case UIDockZone_Left:
            X -= Gap + Size;
            break;
        case UIDockZone_Right:
            X += Size + Gap;
            break;
        case UIDockZone_Top:
            Y -= Gap + Size;
            break;
        case UIDockZone_Bottom:
            Y += Size + Gap;
            break;
        case UIDockZone_Center:
        case UIDockZone_Count:
            break;
    }
    // Kept inside the leaf's box as far as it fits: below its strip where there is room, over the strip where there is not.
    X = Math_MaxFloat(leafBox.x, Math_MinFloat(X, leafBox.x + Content.width - Size));
    Y = Math_MaxFloat(leafBox.y, Math_MinFloat(Math_MaxFloat(Y, Content.y), leafBox.y + CleanLength(leafBox.height) - Size));
    return (Rectangle){ .x = X, .y = Y, .width = Size, .height = Size };
}

Rectangle UIDockZone_GetPreview(Rectangle leafBox, UIDockZone zone, float splitterThickness)
{
    bool IsAcross = (zone == UIDockZone_Left) || (zone == UIDockZone_Right);
    float Total = IsAcross ? leafBox.width : leafBox.height;
    float Splitter = Math_MinFloat(RoundToUnit(CleanLength(splitterThickness)), CleanLength(Total));
    float Inner = Math_MaxFloat(0.0f, Total - Splitter);
    float First = GetFirstLength(Inner, UI_DOCK_TREE_DEFAULT_RATIO, 0.0f, 0.0f);
    Rectangle Preview = leafBox;
    switch (zone)
    {
        case UIDockZone_Left:
            Preview.width = First;
            break;
        case UIDockZone_Top:
            Preview.height = First;
            break;
        case UIDockZone_Right:
            Preview.x = leafBox.x + First + Splitter;
            Preview.width = Inner - First;
            break;
        case UIDockZone_Bottom:
            Preview.y = leafBox.y + First + Splitter;
            Preview.height = Inner - First;
            break;
        case UIDockZone_Center:
        case UIDockZone_Count:
            break;
    }
    return Preview;
}
