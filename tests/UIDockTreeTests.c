#include "TestSuites.h"
#include "UIDockTree.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"
#include <math.h>
#include <stdio.h>


// Macros.
/** The splitters' thickness the arranging tests use. */
#define SPLITTER 4.0f


// Types.
/* A minimum per leaf, by the leaf's first item. */
typedef struct MinimumTableStruct
{
    Vector2 PerItem[UI_DOCK_TREE_MAX_TABS + 2];
} MinimumTable;


// Static functions: helpers.
static uint64_t NextRandom(uint64_t* state)
{
    *state = (*state * 6364136223846793005ULL) + 1442695040888963407ULL;
    return *state >> 33U;
}

static Vector2 MinimumOfFirstItem(const UIDockTree* tree, int32_t leaf, const UserData* userData)
{
    const MinimumTable* Table = UserData_GetPointer(userData);
    uint64_t Item = UIDockTree_GetTabAt(tree, leaf, 0U);
    return (Item < (uint64_t)(UI_DOCK_TREE_MAX_TABS + 2)) ? Table->PerItem[Item] : (Vector2){ .x = 0.0f, .y = 0.0f };
}

static Vector2 FixedMinimum(const UIDockTree* tree, int32_t leaf, const UserData* userData)
{
    (void)tree;
    (void)leaf;
    const Vector2* Minimum = UserData_GetPointer(userData);
    return *Minimum;
}

static bool IsWhole(float value)
{
    return Math_FloorFloat(value) == value;
}

static bool IsBoxWhole(Rectangle box)
{
    return IsWhole(box.x) && IsWhole(box.y) && IsWhole(box.width) && IsWhole(box.height);
}

static float Overlap(Rectangle a, Rectangle b)
{
    float Width = Math_MinFloat(a.x + a.width, b.x + b.width) - Math_MaxFloat(a.x, b.x);
    float Height = Math_MinFloat(a.y + a.height, b.y + b.height) - Math_MaxFloat(a.y, b.y);
    return ((Width > 0.0f) && (Height > 0.0f)) ? (Width * Height) : 0.0f;
}

static bool IsInside(Rectangle inner, Rectangle outer)
{
    return (inner.x >= outer.x) && (inner.y >= outer.y) && ((inner.x + inner.width) <= (outer.x + outer.width))
        && ((inner.y + inner.height) <= (outer.y + outer.height));
}

/* The leaves' and splitters' boxes cover the area exactly: whole units, inside it, no two overlapping, their areas adding up to its. */
static bool IsTiling(const UIDockTree* tree, const UIDockArrangement* arrangement)
{
    Rectangle Pieces[2 * UI_DOCK_TREE_MAX_NODES];
    size_t Count = 0U;
    for (int32_t Index = 0; Index < (int32_t)UI_DOCK_TREE_MAX_NODES; Index++)
    {
        if (UIDockTree_IsLeaf(tree, Index))
        {
            Pieces[Count++] = arrangement->NodeBoxes[Index];
        }
        else if (UIDockTree_IsSplit(tree, Index))
        {
            Pieces[Count++] = arrangement->SplitterBoxes[Index];
        }
    }
    double Covered = 0.0;
    for (size_t Index = 0U; Index < Count; Index++)
    {
        if (!IsBoxWhole(Pieces[Index]) || (Pieces[Index].width < 0.0f) || (Pieces[Index].height < 0.0f)
            || !IsInside(Pieces[Index], arrangement->Area))
        {
            return false;
        }
        Covered += (double)Pieces[Index].width * (double)Pieces[Index].height;
        for (size_t Other = Index + 1U; Other < Count; Other++)
        {
            if (Overlap(Pieces[Index], Pieces[Other]) > 0.0f)
            {
                return false;
            }
        }
    }
    double Area = (double)arrangement->Area.width * (double)arrangement->Area.height;
    return UIDockTree_IsEmpty(tree) || (fabs(Covered - Area) < 0.5);
}

/* Root holding 1; 2 at its right; 3 below 2. */
static void BuildThree(TestRun* run, UIDockTree* tree, int32_t* outRight, int32_t* outBelow)
{
    UIDockTree_Construct(tree);
    TEST_CHECK_SUCCESS(run, UIDockTree_AddTab(tree, UI_DOCK_NODE_NONE, 1U, 0U));
    TEST_CHECK_SUCCESS(run, UIDockTree_SplitLeaf(tree, UIDockTree_GetRoot(tree), UIDockZone_Right, 2U, outRight));
    TEST_CHECK_SUCCESS(run, UIDockTree_SplitLeaf(tree, *outRight, UIDockZone_Bottom, 3U, outBelow));
}


// Static functions: test cases.
static void AnEmptyTreeHasNothing(TestRun* run)
{
    UIDockTree Tree;
    UIDockTree_Construct(&Tree);
    TEST_CHECK(run, UIDockTree_IsEmpty(&Tree));
    TEST_CHECK(run, UIDockTree_IsValid(&Tree));
    TEST_CHECK(run, UIDockTree_GetLeafCount(&Tree) == 0U);
    TEST_CHECK(run, !UIDockTree_HasEmptyLeaf(&Tree));
    TEST_CHECK(run, UIDockTree_GetNode(&Tree, 0) == NULL);
    TEST_CHECK(run, UIDockTree_GetNode(&Tree, -1) == NULL);
    TEST_CHECK(run, UIDockTree_GetNode(&Tree, UI_DOCK_TREE_MAX_NODES) == NULL);
    UIDockArrangement Arrangement;
    UIDockTree_Arrange(&Tree, (Rectangle){ .x = 10.4f, .y = 20.6f, .width = 300.0f, .height = 200.0f }, SPLITTER, NULL, NULL, &Arrangement);
    TEST_CHECK(run, (Arrangement.Area.x == 10.0f) && (Arrangement.Area.y == 21.0f) && (Arrangement.Area.width == 300.0f));
    int32_t Leaf = 7;
    UIDockZone Zone = UIDockZone_Left;
    // The area's centre guide: 30 wide around its middle (160, 121).
    TEST_CHECK(run, UIDockTree_DropZoneAt(&Tree, &Arrangement, (Vector2){ .x = 160.0f, .y = 121.0f }, 20.0f, 30.0f, &Leaf, &Zone));
    TEST_CHECK(run, (Leaf == UI_DOCK_NODE_NONE) && (Zone == UIDockZone_Center));
    TEST_CHECK(run, !UIDockTree_DropZoneAt(&Tree, &Arrangement, (Vector2){ .x = 20.0f, .y = 30.0f }, 20.0f, 30.0f, &Leaf, &Zone));
    // Only an empty tree's root comes from nowhere.
    TEST_CHECK_ERROR(run, UIDockTree_AddTab(&Tree, 0, 5U, 0U), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK(run, UIDockTree_IsEmpty(&Tree));
    UIDockTree_Deconstruct(&Tree);
}

static void TabsAreAddedInPlaceSelectedAndRemoved(TestRun* run)
{
    UIDockTree Tree;
    UIDockTree_Construct(&Tree);
    TEST_CHECK_SUCCESS(run, UIDockTree_AddTab(&Tree, UI_DOCK_NODE_NONE, 10U, 0U));
    int32_t Root = UIDockTree_GetRoot(&Tree);
    TEST_CHECK(run, UIDockTree_IsLeaf(&Tree, Root) && (UIDockTree_GetSelected(&Tree, Root) == 10U));
    TEST_CHECK_SUCCESS(run, UIDockTree_AddTab(&Tree, Root, 20U, 99U)); // Past the last: last.
    TEST_CHECK_SUCCESS(run, UIDockTree_AddTab(&Tree, Root, 30U, 0U));  // First.
    TEST_CHECK_SUCCESS(run, UIDockTree_AddTab(&Tree, Root, 40U, 2U));  // Between 10 and 20.
    TEST_CHECK(run, UIDockTree_GetTabCount(&Tree, Root) == 4U);
    TEST_CHECK(run, (UIDockTree_GetTabAt(&Tree, Root, 0U) == 30U) && (UIDockTree_GetTabAt(&Tree, Root, 1U) == 10U)
        && (UIDockTree_GetTabAt(&Tree, Root, 2U) == 40U) && (UIDockTree_GetTabAt(&Tree, Root, 3U) == 20U));
    TEST_CHECK(run, UIDockTree_GetTabAt(&Tree, Root, 4U) == UI_DOCK_ITEM_NONE);
    TEST_CHECK(run, UIDockTree_GetSelected(&Tree, Root) == 40U); // Adding selects.
    size_t Place = 9U;
    int32_t Leaf = UI_DOCK_NODE_NONE;
    TEST_CHECK(run, UIDockTree_FindTab(&Tree, 20U, &Leaf, &Place) && (Leaf == Root) && (Place == 3U));
    TEST_CHECK(run, !UIDockTree_FindTab(&Tree, 50U, &Leaf, &Place));
    TEST_CHECK(run, !UIDockTree_FindTab(&Tree, UI_DOCK_ITEM_NONE, &Leaf, &Place));

    TEST_CHECK_ERROR(run, UIDockTree_AddTab(&Tree, Root, 10U, 0U), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, UIDockTree_AddTab(&Tree, Root, UI_DOCK_ITEM_NONE, 0U), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, UIDockTree_AddTab(&Tree, Root + 1, 60U, 0U), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, UIDockTree_AddTab(&Tree, UI_DOCK_NODE_NONE, 60U, 0U), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK(run, UIDockTree_GetTotalTabCount(&Tree) == 4U);

    // The selected one going: the one after it, else the one before.
    TEST_CHECK_SUCCESS(run, UIDockTree_RemoveTab(&Tree, 40U));
    TEST_CHECK(run, UIDockTree_GetSelected(&Tree, Root) == 20U);
    TEST_CHECK_SUCCESS(run, UIDockTree_RemoveTab(&Tree, 20U));
    TEST_CHECK(run, UIDockTree_GetSelected(&Tree, Root) == 10U);
    TEST_CHECK_SUCCESS(run, UIDockTree_SelectTab(&Tree, 30U));
    TEST_CHECK_SUCCESS(run, UIDockTree_RemoveTab(&Tree, 10U)); // Not the selected: the selection stays.
    TEST_CHECK(run, UIDockTree_GetSelected(&Tree, Root) == 30U);
    TEST_CHECK_SUCCESS(run, UIDockTree_RemoveTab(&Tree, 30U));
    TEST_CHECK(run, UIDockTree_GetSelected(&Tree, Root) == UI_DOCK_ITEM_NONE);
    // A tab after the selected one going: the selection stays where it is.
    TEST_CHECK_SUCCESS(run, UIDockTree_AddTab(&Tree, Root, 70U, 0U));
    TEST_CHECK_SUCCESS(run, UIDockTree_AddTab(&Tree, Root, 71U, 1U));
    TEST_CHECK_SUCCESS(run, UIDockTree_AddTab(&Tree, Root, 72U, 2U));
    TEST_CHECK_SUCCESS(run, UIDockTree_SelectTab(&Tree, 70U));
    TEST_CHECK_SUCCESS(run, UIDockTree_RemoveTab(&Tree, 72U));
    TEST_CHECK(run, UIDockTree_GetSelected(&Tree, Root) == 70U);
    TEST_CHECK_SUCCESS(run, UIDockTree_RemoveTab(&Tree, 70U));
    TEST_CHECK_SUCCESS(run, UIDockTree_RemoveTab(&Tree, 71U));
    TEST_CHECK(run, UIDockTree_GetSelected(&Tree, Root) == UI_DOCK_ITEM_NONE);
    TEST_CHECK(run, UIDockTree_HasEmptyLeaf(&Tree) && UIDockTree_IsValid(&Tree));
    TEST_CHECK_ERROR(run, UIDockTree_RemoveTab(&Tree, 30U), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, UIDockTree_SelectTab(&Tree, 30U), ErrorCode_ArgumentOutOfRange);
    UIDockTree_CollapseEmpty(&Tree);
    TEST_CHECK(run, UIDockTree_IsEmpty(&Tree) && UIDockTree_IsValid(&Tree));
}

static void TabsStopAtTheirLimit(TestRun* run)
{
    UIDockTree Tree;
    UIDockTree_Construct(&Tree);
    TEST_CHECK_SUCCESS(run, UIDockTree_AddTab(&Tree, UI_DOCK_NODE_NONE, 1U, 0U));
    int32_t Root = UIDockTree_GetRoot(&Tree);
    for (uint64_t Item = 2U; Item <= (uint64_t)UI_DOCK_TREE_MAX_TABS; Item++)
    {
        TEST_CHECK_SUCCESS(run, UIDockTree_AddTab(&Tree, Root, Item, 0U));
    }
    TEST_CHECK_ERROR(run, UIDockTree_AddTab(&Tree, Root, 1000U, 0U), ErrorCode_InvalidOperation);
    TEST_CHECK_ERROR(run, UIDockTree_SplitLeaf(&Tree, Root, UIDockZone_Left, 1000U, NULL), ErrorCode_InvalidOperation);
    TEST_CHECK(run, (UIDockTree_GetTotalTabCount(&Tree) == UI_DOCK_TREE_MAX_TABS) && UIDockTree_IsValid(&Tree));
    TEST_CHECK(run, UIDockTree_GetLeafCount(&Tree) == 1U);
}

static void TabsMoveAlongALeafAndToAnother(TestRun* run)
{
    UIDockTree Tree;
    int32_t Right = UI_DOCK_NODE_NONE;
    int32_t Below = UI_DOCK_NODE_NONE;
    BuildThree(run, &Tree, &Right, &Below);
    int32_t Left = UI_DOCK_NODE_NONE;
    TEST_CHECK(run, UIDockTree_FindTab(&Tree, 1U, &Left, NULL));
    TEST_CHECK_SUCCESS(run, UIDockTree_AddTab(&Tree, Left, 4U, 99U));
    TEST_CHECK_SUCCESS(run, UIDockTree_AddTab(&Tree, Left, 5U, 99U));
    // Reordering along a leaf: 1 4 5 -> 4 5 1.
    TEST_CHECK_SUCCESS(run, UIDockTree_MoveTab(&Tree, 1U, Left, 2U));
    TEST_CHECK(run, (UIDockTree_GetTabAt(&Tree, Left, 0U) == 4U) && (UIDockTree_GetTabAt(&Tree, Left, 1U) == 5U)
        && (UIDockTree_GetTabAt(&Tree, Left, 2U) == 1U));
    TEST_CHECK(run, UIDockTree_GetSelected(&Tree, Left) == 1U);
    // To another leaf: selected there; the leaf it left selects the next.
    TEST_CHECK_SUCCESS(run, UIDockTree_SelectTab(&Tree, 4U));
    TEST_CHECK_SUCCESS(run, UIDockTree_MoveTab(&Tree, 4U, Below, 0U));
    TEST_CHECK(run, (UIDockTree_GetTabAt(&Tree, Below, 0U) == 4U) && (UIDockTree_GetSelected(&Tree, Below) == 4U));
    TEST_CHECK(run, UIDockTree_GetSelected(&Tree, Left) == 5U);
    // Emptying a leaf by moves: it stays until collapsed.
    TEST_CHECK_SUCCESS(run, UIDockTree_MoveTab(&Tree, 2U, Below, 99U));
    TEST_CHECK(run, UIDockTree_GetTabCount(&Tree, Right) == 0U);
    TEST_CHECK(run, UIDockTree_IsValid(&Tree) && UIDockTree_HasEmptyLeaf(&Tree));
    UIDockTree_CollapseEmpty(&Tree);
    TEST_CHECK(run, UIDockTree_IsValid(&Tree) && !UIDockTree_HasEmptyLeaf(&Tree));
    TEST_CHECK(run, UIDockTree_GetLeafCount(&Tree) == 2U);
    TEST_CHECK_ERROR(run, UIDockTree_MoveTab(&Tree, 77U, Below, 0U), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, UIDockTree_MoveTab(&Tree, 5U, UI_DOCK_NODE_NONE, 0U), ErrorCode_ArgumentOutOfRange);
}

static void ALeafSplitsOnEachSide(TestRun* run)
{
    static const UIDockZone Sides[4] = { UIDockZone_Left, UIDockZone_Top, UIDockZone_Right, UIDockZone_Bottom };
    for (size_t Side = 0U; Side < 4U; Side++)
    {
        UIDockTree Tree;
        UIDockTree_Construct(&Tree);
        TEST_CHECK_SUCCESS(run, UIDockTree_AddTab(&Tree, UI_DOCK_NODE_NONE, 1U, 0U));
        int32_t Old = UIDockTree_GetRoot(&Tree);
        int32_t New = UI_DOCK_NODE_NONE;
        TEST_CHECK_SUCCESS(run, UIDockTree_SplitLeaf(&Tree, Old, Sides[Side], 2U, &New));
        const UIDockNode* Split = UIDockTree_GetNode(&Tree, UIDockTree_GetRoot(&Tree));
        TEST_CHECK(run, (Split != NULL) && (Split->Kind == UIDockNodeKind_Split));
        if (Split == NULL)
        {
            continue;
        }
        bool IsNewFirst = (Sides[Side] == UIDockZone_Left) || (Sides[Side] == UIDockZone_Top);
        bool IsAcross = (Sides[Side] == UIDockZone_Left) || (Sides[Side] == UIDockZone_Right);
        TEST_CHECK(run, Split->Split == (IsAcross ? UIDockSplit_Across : UIDockSplit_Down));
        TEST_CHECK(run, Split->Children[0] == (IsNewFirst ? New : Old));
        TEST_CHECK(run, Split->Children[1] == (IsNewFirst ? Old : New));
        TEST_CHECK(run, Split->Ratio == UI_DOCK_TREE_DEFAULT_RATIO);
        TEST_CHECK(run, (UIDockTree_GetSelected(&Tree, New) == 2U) && (UIDockTree_GetTabCount(&Tree, New) == 1U));
        TEST_CHECK(run, UIDockTree_GetDepth(&Tree, New) == 1U);
        TEST_CHECK(run, UIDockTree_IsValid(&Tree));
    }
    UIDockTree Tree;
    UIDockTree_Construct(&Tree);
    TEST_CHECK_SUCCESS(run, UIDockTree_AddTab(&Tree, UI_DOCK_NODE_NONE, 1U, 0U));
    int32_t Root = UIDockTree_GetRoot(&Tree);
    TEST_CHECK_ERROR(run, UIDockTree_SplitLeaf(&Tree, Root, UIDockZone_Center, 2U, NULL), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, UIDockTree_SplitLeaf(&Tree, Root, UIDockZone_Left, 1U, NULL), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, UIDockTree_SplitLeaf(&Tree, Root, UIDockZone_Left, UI_DOCK_ITEM_NONE, NULL), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, UIDockTree_SplitLeaf(&Tree, Root + 5, UIDockZone_Left, 2U, NULL), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK(run, (UIDockTree_GetLeafCount(&Tree) == 1U) && UIDockTree_IsValid(&Tree));
}

static void TheDepthAndLeafLimitsHold(TestRun* run)
{
    // Depth: always splitting the newest leaf goes one level deeper each time.
    UIDockTree Tree;
    UIDockTree_Construct(&Tree);
    TEST_CHECK_SUCCESS(run, UIDockTree_AddTab(&Tree, UI_DOCK_NODE_NONE, 1U, 0U));
    int32_t Deepest = UIDockTree_GetRoot(&Tree);
    for (uint64_t Item = 2U; Item < (uint64_t)(UI_DOCK_TREE_MAX_DEPTH + 2); Item++)
    {
        TEST_CHECK_SUCCESS(run, UIDockTree_SplitLeaf(&Tree, Deepest, ((Item % 2U) == 0U) ? UIDockZone_Right : UIDockZone_Bottom, Item, &Deepest));
    }
    TEST_CHECK(run, UIDockTree_GetDepth(&Tree, Deepest) == UI_DOCK_TREE_MAX_DEPTH);
    TEST_CHECK_ERROR(run, UIDockTree_SplitLeaf(&Tree, Deepest, UIDockZone_Left, 500U, NULL), ErrorCode_InvalidOperation);
    TEST_CHECK(run, UIDockTree_IsValid(&Tree));

    // Built node by node (as a file is read): a chain of splits as deep as allowed, and no deeper.
    UIDockTree_Construct(&Tree);
    int32_t Parent = UI_DOCK_NODE_NONE;
    TEST_CHECK_SUCCESS(run, UIDockTree_BuildNode(&Tree, UI_DOCK_NODE_NONE, 0, UIDockNodeKind_Split, UIDockSplit_Across, 0.5f, &Parent));
    for (size_t Level = 1U; Level < (size_t)UI_DOCK_TREE_MAX_DEPTH; Level++)
    {
        int32_t Leaf = UI_DOCK_NODE_NONE;
        TEST_CHECK_SUCCESS(run, UIDockTree_BuildNode(&Tree, Parent, 0, UIDockNodeKind_Leaf, UIDockSplit_Across, 0.0f, &Leaf));
        TEST_CHECK_SUCCESS(run, UIDockTree_BuildNode(&Tree, Parent, 1, UIDockNodeKind_Split, UIDockSplit_Down, 0.5f, &Parent));
    }
    int32_t Last = UI_DOCK_NODE_NONE;
    TEST_CHECK_SUCCESS(run, UIDockTree_BuildNode(&Tree, Parent, 0, UIDockNodeKind_Leaf, UIDockSplit_Across, 0.0f, &Last));
    TEST_CHECK(run, UIDockTree_GetDepth(&Tree, Last) == UI_DOCK_TREE_MAX_DEPTH);
    int32_t Bottom = UI_DOCK_NODE_NONE;
    TEST_CHECK_SUCCESS(run, UIDockTree_BuildNode(&Tree, Parent, 1, UIDockNodeKind_Split, UIDockSplit_Down, 0.5f, &Bottom)); // As deep as nodes go...
    TEST_CHECK_ERROR(run, UIDockTree_BuildNode(&Tree, Bottom, 0, UIDockNodeKind_Leaf, UIDockSplit_Across, 0.0f, NULL),
        ErrorCode_InvalidOperation); // ...so none below it.
    TEST_CHECK(run, !UIDockTree_IsValid(&Tree)); // A split missing its children holds nothing together.

    // Leaves: splitting the root's leaf each time keeps the tree shallow and fills the leaves.
    UIDockTree_Construct(&Tree);
    TEST_CHECK_SUCCESS(run, UIDockTree_AddTab(&Tree, UI_DOCK_NODE_NONE, 1U, 0U));
    uint64_t Item = 2U;
    for (size_t Round = 0U; (UIDockTree_GetLeafCount(&Tree) < UI_DOCK_TREE_MAX_LEAVES) && (Round < 200U); Round++)
    {
        int32_t Shallowest = UI_DOCK_NODE_NONE;
        for (int32_t Index = 0; Index < (int32_t)UI_DOCK_TREE_MAX_NODES; Index++)
        {
            if (UIDockTree_IsLeaf(&Tree, Index) && ((Shallowest == UI_DOCK_NODE_NONE)
                || (UIDockTree_GetDepth(&Tree, Index) < UIDockTree_GetDepth(&Tree, Shallowest))))
            {
                Shallowest = Index;
            }
        }
        TEST_CHECK_SUCCESS(run, UIDockTree_SplitLeaf(&Tree, Shallowest, UIDockZone_Left, Item++, NULL));
    }
    TEST_CHECK(run, UIDockTree_GetLeafCount(&Tree) == UI_DOCK_TREE_MAX_LEAVES);
    TEST_CHECK_ERROR(run, UIDockTree_SplitLeaf(&Tree, UIDockTree_GetRoot(&Tree), UIDockZone_Left, 999U, NULL),
        ErrorCode_ArgumentOutOfRange); // A split is no leaf.
    int32_t AnyLeaf = UI_DOCK_NODE_NONE;
    TEST_CHECK(run, UIDockTree_FindTab(&Tree, 1U, &AnyLeaf, NULL));
    TEST_CHECK_ERROR(run, UIDockTree_SplitLeaf(&Tree, AnyLeaf, UIDockZone_Left, 999U, NULL), ErrorCode_InvalidOperation);
    TEST_CHECK(run, UIDockTree_IsValid(&Tree));
}

static void EmptyLeavesCollapse(TestRun* run)
{
    UIDockTree Tree;
    int32_t Right = UI_DOCK_NODE_NONE;
    int32_t Below = UI_DOCK_NODE_NONE;
    BuildThree(run, &Tree, &Right, &Below);
    int32_t RightSplit = UIDockTree_GetNode(&Tree, Right)->Parent;
    TEST_CHECK_SUCCESS(run, UIDockTree_SetRatio(&Tree, RightSplit, 0.3f));
    // 2 goes: its split gives its place to 3's leaf, under the root.
    TEST_CHECK_SUCCESS(run, UIDockTree_RemoveTab(&Tree, 2U));
    UIDockTree_CollapseEmpty(&Tree);
    TEST_CHECK(run, UIDockTree_IsValid(&Tree) && (UIDockTree_GetLeafCount(&Tree) == 2U));
    const UIDockNode* Root = UIDockTree_GetNode(&Tree, UIDockTree_GetRoot(&Tree));
    TEST_CHECK(run, (Root != NULL) && (Root->Kind == UIDockNodeKind_Split) && (Root->Children[1] == Below));
    TEST_CHECK(run, UIDockTree_GetNode(&Tree, Below)->Parent == UIDockTree_GetRoot(&Tree));
    // 1 goes: 3's leaf becomes the root.
    TEST_CHECK_SUCCESS(run, UIDockTree_RemoveTab(&Tree, 1U));
    UIDockTree_CollapseEmpty(&Tree);
    TEST_CHECK(run, UIDockTree_IsValid(&Tree) && (UIDockTree_GetRoot(&Tree) == Below));
    TEST_CHECK(run, UIDockTree_GetNode(&Tree, Below)->Parent == UI_DOCK_NODE_NONE);
    // Two empty leaves under one split go together.
    BuildThree(run, &Tree, &Right, &Below);
    TEST_CHECK_SUCCESS(run, UIDockTree_RemoveTab(&Tree, 2U));
    TEST_CHECK_SUCCESS(run, UIDockTree_RemoveTab(&Tree, 3U));
    UIDockTree_CollapseEmpty(&Tree);
    TEST_CHECK(run, UIDockTree_IsValid(&Tree) && (UIDockTree_GetLeafCount(&Tree) == 1U));
    TEST_CHECK(run, UIDockTree_GetSelected(&Tree, UIDockTree_GetRoot(&Tree)) == 1U);
}

static void RatiosAndItemsChangeAndCopy(TestRun* run)
{
    UIDockTree Tree;
    int32_t Right = UI_DOCK_NODE_NONE;
    int32_t Below = UI_DOCK_NODE_NONE;
    BuildThree(run, &Tree, &Right, &Below);
    int32_t Root = UIDockTree_GetRoot(&Tree);
    TEST_CHECK_SUCCESS(run, UIDockTree_SetRatio(&Tree, Root, 1.7f));
    TEST_CHECK(run, UIDockTree_GetNode(&Tree, Root)->Ratio == 1.0f);
    TEST_CHECK_SUCCESS(run, UIDockTree_SetRatio(&Tree, Root, -3.0f));
    TEST_CHECK(run, UIDockTree_GetNode(&Tree, Root)->Ratio == 0.0f);
    TEST_CHECK_ERROR(run, UIDockTree_SetRatio(&Tree, Root, NAN), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, UIDockTree_SetRatio(&Tree, Root, INFINITY), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, UIDockTree_SetRatio(&Tree, Below, 0.5f), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK(run, UIDockTree_GetNode(&Tree, Root)->Ratio == 0.0f);

    TEST_CHECK_SUCCESS(run, UIDockTree_ReplaceItem(&Tree, 3U, 33U));
    TEST_CHECK(run, (UIDockTree_GetSelected(&Tree, Below) == 33U) && !UIDockTree_FindTab(&Tree, 3U, NULL, NULL));
    TEST_CHECK_SUCCESS(run, UIDockTree_ReplaceItem(&Tree, 33U, 33U));
    TEST_CHECK_ERROR(run, UIDockTree_ReplaceItem(&Tree, 33U, 1U), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, UIDockTree_ReplaceItem(&Tree, 33U, UI_DOCK_ITEM_NONE), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, UIDockTree_ReplaceItem(&Tree, 3U, 34U), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK(run, UIDockTree_IsValid(&Tree));

    // A copy is a whole tree of its own.
    UIDockTree Copy = Tree;
    TEST_CHECK_SUCCESS(run, UIDockTree_RemoveTab(&Copy, 33U));
    UIDockTree_CollapseEmpty(&Copy);
    TEST_CHECK(run, UIDockTree_IsValid(&Copy) && UIDockTree_FindTab(&Tree, 33U, NULL, NULL));
}

static void SplitsAreArrangedWithMinimums(TestRun* run)
{
    UIDockTree Tree;
    UIDockTree_Construct(&Tree);
    TEST_CHECK_SUCCESS(run, UIDockTree_AddTab(&Tree, UI_DOCK_NODE_NONE, 1U, 0U));
    int32_t Left = UIDockTree_GetRoot(&Tree);
    int32_t Right = UI_DOCK_NODE_NONE;
    TEST_CHECK_SUCCESS(run, UIDockTree_SplitLeaf(&Tree, Left, UIDockZone_Right, 2U, &Right));
    int32_t Root = UIDockTree_GetRoot(&Tree);
    TEST_CHECK_SUCCESS(run, UIDockTree_SetRatio(&Tree, Root, 0.25f));
    UIDockArrangement Arrangement;
    UIDockTree_Arrange(&Tree, (Rectangle){ .x = 0.0f, .y = 30.0f, .width = 1004.0f, .height = 500.0f }, SPLITTER, NULL, NULL, &Arrangement);
    // 1000 shared: 250 and 750 around the splitter.
    TEST_CHECK(run, (Arrangement.NodeBoxes[Left].x == 0.0f) && (Arrangement.NodeBoxes[Left].width == 250.0f));
    TEST_CHECK(run, (Arrangement.SplitterBoxes[Root].x == 250.0f) && (Arrangement.SplitterBoxes[Root].width == SPLITTER));
    TEST_CHECK(run, (Arrangement.NodeBoxes[Right].x == 254.0f) && (Arrangement.NodeBoxes[Right].width == 750.0f));
    TEST_CHECK(run, (Arrangement.NodeBoxes[Right].y == 30.0f) && (Arrangement.NodeBoxes[Right].height == 500.0f));
    TEST_CHECK(run, IsTiling(&Tree, &Arrangement));

    // Minimums hold while there is room for both.
    MinimumTable Table = { 0 };
    Table.PerItem[1] = (Vector2){ .x = 400.0f, .y = 100.0f };
    Table.PerItem[2] = (Vector2){ .x = 300.0f, .y = 120.0f };
    UserData Data = UserData_FromPointer(&Table);
    UIDockTree_Arrange(&Tree, (Rectangle){ .x = 0.0f, .y = 0.0f, .width = 1004.0f, .height = 500.0f }, SPLITTER, MinimumOfFirstItem, &Data,
        &Arrangement);
    TEST_CHECK(run, Arrangement.NodeBoxes[Left].width == 400.0f);
    TEST_CHECK(run, (Arrangement.Minimums[Root].x == 704.0f) && (Arrangement.Minimums[Root].y == 120.0f));
    TEST_CHECK(run, IsTiling(&Tree, &Arrangement));
    TEST_CHECK_SUCCESS(run, UIDockTree_SetRatio(&Tree, Root, 0.95f));
    UIDockTree_Arrange(&Tree, (Rectangle){ .x = 0.0f, .y = 0.0f, .width = 1004.0f, .height = 500.0f }, SPLITTER, MinimumOfFirstItem, &Data,
        &Arrangement);
    TEST_CHECK(run, Arrangement.NodeBoxes[Right].width == 300.0f);
    // Below both minimums: the same share of each one's.
    UIDockTree_Arrange(&Tree, (Rectangle){ .x = 0.0f, .y = 0.0f, .width = 354.0f, .height = 500.0f }, SPLITTER, MinimumOfFirstItem, &Data,
        &Arrangement);
    TEST_CHECK(run, (Arrangement.NodeBoxes[Left].width == 200.0f) && (Arrangement.NodeBoxes[Right].width == 150.0f));
    TEST_CHECK(run, IsTiling(&Tree, &Arrangement));
    // Narrower than the splitter: nothing for the sides, no gaps.
    UIDockTree_Arrange(&Tree, (Rectangle){ .x = 0.0f, .y = 0.0f, .width = 3.0f, .height = 50.0f }, SPLITTER, MinimumOfFirstItem, &Data,
        &Arrangement);
    TEST_CHECK(run, (Arrangement.SplitterBoxes[Root].width == 3.0f) && (Arrangement.NodeBoxes[Left].width == 0.0f));
    TEST_CHECK(run, IsTiling(&Tree, &Arrangement));
    // Bad minimums count as none; the area's edges are rounded.
    Table.PerItem[1] = (Vector2){ .x = NAN, .y = -5.0f };
    Table.PerItem[2] = (Vector2){ .x = INFINITY, .y = 1e30f };
    UIDockTree_Arrange(&Tree, (Rectangle){ .x = 0.4f, .y = 0.6f, .width = 999.2f, .height = 499.0f }, 3.6f, MinimumOfFirstItem, &Data, &Arrangement);
    TEST_CHECK(run, (Arrangement.Area.x == 0.0f) && (Arrangement.Area.y == 1.0f) && (Arrangement.Area.width == 1000.0f)
        && (Arrangement.Area.height == 499.0f) && (Arrangement.SplitterThickness == 4.0f));
    TEST_CHECK(run, IsTiling(&Tree, &Arrangement));
}

/* Random trees arranged at many sizes and minimums: every arrangement tiles its area. */
static void RandomTreesTileTheAreaAtManySizes(TestRun* run)
{
    uint64_t State = 12345U;
    size_t Failures = 0U;
    for (size_t Round = 0U; Round < 300U; Round++)
    {
        UIDockTree Tree;
        UIDockTree_Construct(&Tree);
        (void)UIDockTree_AddTab(&Tree, UI_DOCK_NODE_NONE, 1U, 0U);
        size_t Leaves = 1U + (size_t)(NextRandom(&State) % 12U);
        for (uint64_t Item = 2U; UIDockTree_GetLeafCount(&Tree) < Leaves; Item++)
        {
            int32_t Leaf = UI_DOCK_NODE_NONE;
            (void)UIDockTree_FindTab(&Tree, 1U + (NextRandom(&State) % (Item - 1U)), &Leaf, NULL);
            UIDockZone Side = (UIDockZone)(1 + (int32_t)(NextRandom(&State) % 4U));
            Error Result = UIDockTree_SplitLeaf(&Tree, Leaf, Side, Item, NULL);
            Error_Deconstruct(&Result);
            if (Item > 64U)
            {
                break;
            }
        }
        for (int32_t Index = 0; Index < (int32_t)UI_DOCK_TREE_MAX_NODES; Index++)
        {
            Error Result = UIDockTree_SetRatio(&Tree, Index, (float)(NextRandom(&State) % 1001U) / 1000.0f);
            Error_Deconstruct(&Result);
        }
        Vector2 Minimum = { .x = (float)(NextRandom(&State) % 120U), .y = (float)(NextRandom(&State) % 90U) };
        UserData Data = UserData_FromPointer(&Minimum);
        Rectangle Area = { .x = (float)(NextRandom(&State) % 50U), .y = (float)(NextRandom(&State) % 50U),
            .width = (float)(NextRandom(&State) % 2600U), .height = (float)(NextRandom(&State) % 1500U) };
        UIDockArrangement Arrangement;
        UIDockTree_Arrange(&Tree, Area, (float)(NextRandom(&State) % 8U), FixedMinimum, &Data, &Arrangement);
        if (!IsTiling(&Tree, &Arrangement) || !UIDockTree_IsValid(&Tree))
        {
            Failures++;
            continue;
        }
        // With room for every minimum, every leaf gets at least its own.
        bool HasRoom = (Arrangement.Minimums[UIDockTree_GetRoot(&Tree)].x <= Arrangement.Area.width)
            && (Arrangement.Minimums[UIDockTree_GetRoot(&Tree)].y <= Arrangement.Area.height);
        for (int32_t Index = 0; HasRoom && (Index < (int32_t)UI_DOCK_TREE_MAX_NODES); Index++)
        {
            if (UIDockTree_IsLeaf(&Tree, Index) && ((Arrangement.NodeBoxes[Index].width < Minimum.x) || (Arrangement.NodeBoxes[Index].height < Minimum.y)))
            {
                Failures++;
                break;
            }
        }
    }
    TEST_CHECK(run, Failures == 0U);
}

/* Lengths below 0 count as 0: a splitter's thickness, a leaf's least size, a strip's height and a guide's size. */
static void LengthsBelowZeroCountAsZero(TestRun* run)
{
    UIDockTree Tree;
    int32_t Right = UI_DOCK_NODE_NONE;
    int32_t Below = UI_DOCK_NODE_NONE;
    BuildThree(run, &Tree, &Right, &Below);
    Rectangle Area = { .x = 0.0f, .y = 0.0f, .width = 600.0f, .height = 300.0f };
    UIDockArrangement Zero;
    UIDockArrangement Negative;
    UIDockTree_Arrange(&Tree, Area, 0.0f, NULL, NULL, &Zero);
    UIDockTree_Arrange(&Tree, Area, -6.0f, NULL, NULL, &Negative);
    TEST_CHECK(run, (Negative.SplitterThickness == 0.0f) && Memory_IsEqual(Zero.NodeBoxes, Negative.NodeBoxes, sizeof(Zero.NodeBoxes)));
    Vector2 Below0 = { .x = -500.0f, .y = -500.0f };
    UserData Data = UserData_FromPointer(&Below0);
    UIDockTree_Arrange(&Tree, Area, SPLITTER, FixedMinimum, &Data, &Negative);
    UIDockTree_Arrange(&Tree, Area, SPLITTER, NULL, NULL, &Zero);
    TEST_CHECK(run, Memory_IsEqual(Zero.NodeBoxes, Negative.NodeBoxes, sizeof(Zero.NodeBoxes)));
    TEST_CHECK(run, (Negative.Minimums[UIDockTree_GetRoot(&Tree)].x == SPLITTER) && (Negative.Minimums[Right].x == 0.0f));
    Rectangle Leaf = { .x = 0.0f, .y = 0.0f, .width = 100.0f, .height = 100.0f };
    Rectangle Guide = UIDockZone_GetGuide(Leaf, -20.0f, UIDockZone_Center, 30.0f);
    TEST_CHECK(run, (Guide.y == 35.0f) && (Guide.height == 30.0f)); // No strip.
    Guide = UIDockZone_GetGuide(Leaf, 20.0f, UIDockZone_Center, -30.0f);
    TEST_CHECK(run, (Guide.width == 0.0f) && (Guide.height == 0.0f));
    int32_t Found = UI_DOCK_NODE_NONE;
    UIDockZone Zone = UIDockZone_Count;
    UIDockArrangement Plain;
    UIDockTree_Arrange(&Tree, Area, SPLITTER, NULL, NULL, &Plain);
    Rectangle Box = Plain.NodeBoxes[Below];
    TEST_CHECK(run, !UIDockTree_DropZoneAt(&Tree, &Plain, (Vector2){ .x = Box.x + 2.0f, .y = Box.y + 2.0f }, -20.0f, 30.0f, &Found, &Zone));
    TEST_CHECK(run, Found == Below); // No strip to drop a tab on.
}

static void ASplittersDragSetsItsRatio(TestRun* run)
{
    UIDockTree Tree;
    int32_t Right = UI_DOCK_NODE_NONE;
    int32_t Below = UI_DOCK_NODE_NONE;
    BuildThree(run, &Tree, &Right, &Below);
    int32_t Root = UIDockTree_GetRoot(&Tree);
    int32_t Inner = UIDockTree_GetNode(&Tree, Right)->Parent;
    UIDockArrangement Arrangement;
    Vector2 Minimum = { .x = 100.0f, .y = 50.0f };
    UserData Data = UserData_FromPointer(&Minimum);
    UIDockTree_Arrange(&Tree, (Rectangle){ .x = 0.0f, .y = 0.0f, .width = 804.0f, .height = 404.0f }, SPLITTER, FixedMinimum, &Data, &Arrangement);
    // The splitter's middle under the pointer: 302 less half the splitter is 300 of 800.
    TEST_CHECK(run, fabsf(UIDockTree_GetRatioAt(&Tree, &Arrangement, Root, (Vector2){ .x = 302.0f, .y = 10.0f }) - 0.375f) < 1e-6f);
    // Kept so each side keeps its minimum.
    TEST_CHECK(run, fabsf(UIDockTree_GetRatioAt(&Tree, &Arrangement, Root, (Vector2){ .x = 5.0f, .y = 10.0f }) - 0.125f) < 1e-6f);
    TEST_CHECK(run, fabsf(UIDockTree_GetRatioAt(&Tree, &Arrangement, Root, (Vector2){ .x = 900.0f, .y = 10.0f }) - 0.875f) < 1e-6f);
    // The inner split goes down: its own box's top.
    float Top = Arrangement.NodeBoxes[Inner].y;
    TEST_CHECK(run, fabsf(UIDockTree_GetRatioAt(&Tree, &Arrangement, Inner, (Vector2){ .x = 600.0f, .y = Top + 202.0f }) - 0.5f) < 1e-6f);
    // No room to share, or no split: the ratio stays.
    TEST_CHECK_SUCCESS(run, UIDockTree_SetRatio(&Tree, Root, 0.6f));
    UIDockTree_Arrange(&Tree, (Rectangle){ .x = 0.0f, .y = 0.0f, .width = 150.0f, .height = 404.0f }, SPLITTER, FixedMinimum, &Data, &Arrangement);
    TEST_CHECK(run, UIDockTree_GetRatioAt(&Tree, &Arrangement, Root, (Vector2){ .x = 10.0f, .y = 10.0f }) == 0.6f);
    TEST_CHECK(run, UIDockTree_GetRatioAt(&Tree, &Arrangement, Below, (Vector2){ .x = 10.0f, .y = 10.0f }) == UI_DOCK_TREE_DEFAULT_RATIO);
}

static void DropZonesAreStripsAndGuides(TestRun* run)
{
    UIDockTree Tree;
    UIDockTree_Construct(&Tree);
    UIDockArrangement Arrangement;
    UIDockTree_Arrange(&Tree, (Rectangle){ .x = 0.0f, .y = 0.0f, .width = 804.0f, .height = 400.0f }, SPLITTER, NULL, NULL, &Arrangement);
    int32_t Leaf = 5;
    UIDockZone Zone = UIDockZone_Count;
    // An empty dock: its area's centre guide alone.
    TEST_CHECK(run, UIDockTree_DropZoneAt(&Tree, &Arrangement, (Vector2){ .x = 400.0f, .y = 200.0f }, 20.0f, 30.0f, &Leaf, &Zone));
    TEST_CHECK(run, (Leaf == UI_DOCK_NODE_NONE) && (Zone == UIDockZone_Center));
    TEST_CHECK(run, !UIDockTree_DropZoneAt(&Tree, &Arrangement, (Vector2){ .x = 300.0f, .y = 200.0f }, 20.0f, 30.0f, &Leaf, &Zone));

    TEST_CHECK_SUCCESS(run, UIDockTree_AddTab(&Tree, UI_DOCK_NODE_NONE, 1U, 0U));
    int32_t Left = UIDockTree_GetRoot(&Tree);
    int32_t Right = UI_DOCK_NODE_NONE;
    TEST_CHECK_SUCCESS(run, UIDockTree_SplitLeaf(&Tree, Left, UIDockZone_Right, 2U, &Right));
    UIDockTree_Arrange(&Tree, (Rectangle){ .x = 0.0f, .y = 0.0f, .width = 804.0f, .height = 400.0f }, SPLITTER, NULL, NULL, &Arrangement);
    // The left leaf is 0..400 across and down, its strip 20 high; its guides 30 wide, 5 apart, the centre's at 185, 195.
    struct { Vector2 Point; int32_t Leaf; bool IsTarget; UIDockZone Zone; } Cases[] =
    {
        { { 200.0f, 210.0f }, 0, true, UIDockZone_Center },
        { { 160.0f, 210.0f }, 0, true, UIDockZone_Left },
        { { 230.0f, 210.0f }, 0, true, UIDockZone_Right },
        { { 200.0f, 170.0f }, 0, true, UIDockZone_Top },
        { { 200.0f, 240.0f }, 0, true, UIDockZone_Bottom },
        { { 10.0f, 10.0f }, 0, true, UIDockZone_Center },     // The strip adds a tab.
        { { 50.0f, 300.0f }, 0, false, UIDockZone_Center },   // Elsewhere on the leaf: it floats.
        { { 182.0f, 210.0f }, 0, false, UIDockZone_Center },  // Between two guides.
        { { 600.0f, 210.0f }, 1, true, UIDockZone_Center },
        { { 640.0f, 210.0f }, 1, true, UIDockZone_Right },
    };
    for (size_t Index = 0U; Index < (sizeof(Cases) / sizeof(Cases[0])); Index++)
    {
        Leaf = UI_DOCK_NODE_NONE;
        Zone = UIDockZone_Count;
        bool IsTarget = UIDockTree_DropZoneAt(&Tree, &Arrangement, Cases[Index].Point, 20.0f, 30.0f, &Leaf, &Zone);
        TEST_CHECK(run, IsTarget == Cases[Index].IsTarget);
        TEST_CHECK(run, Leaf == ((Cases[Index].Leaf == 0) ? Left : Right));
        TEST_CHECK(run, !IsTarget || (Zone == Cases[Index].Zone));
    }
    TEST_CHECK(run, !UIDockTree_DropZoneAt(&Tree, &Arrangement, (Vector2){ .x = 401.0f, .y = 200.0f }, 20.0f, 30.0f, &Leaf, &Zone)); // A splitter.
    TEST_CHECK(run, Leaf == UI_DOCK_NODE_NONE);
    TEST_CHECK(run, !UIDockTree_DropZoneAt(&Tree, &Arrangement, (Vector2){ .x = 400.0f, .y = 300.0f }, 20.0f, 30.0f, &Leaf, &Zone)); // Its first pixel.
    TEST_CHECK(run, Leaf == UI_DOCK_NODE_NONE);
    TEST_CHECK(run, !UIDockTree_DropZoneAt(&Tree, &Arrangement, (Vector2){ .x = 900.0f, .y = 200.0f }, 20.0f, 30.0f, &Leaf, &Zone));
    TEST_CHECK(run, !UIDockTree_DropZoneAt(&Tree, &Arrangement, (Vector2){ .x = NAN, .y = 200.0f }, 20.0f, 30.0f, &Leaf, &Zone));

    // Each guide's box: the centre's in the middle under the strip, the others a sixth of a guide away from it.
    Rectangle LeftBox = Arrangement.NodeBoxes[Left];
    struct { UIDockZone Zone; float X; float Y; } Guides[] =
    {
        { UIDockZone_Center, 185.0f, 195.0f }, { UIDockZone_Left, 150.0f, 195.0f }, { UIDockZone_Right, 220.0f, 195.0f },
        { UIDockZone_Top, 185.0f, 160.0f }, { UIDockZone_Bottom, 185.0f, 230.0f },
    };
    for (size_t Index = 0U; Index < (sizeof(Guides) / sizeof(Guides[0])); Index++)
    {
        Rectangle Box = UIDockZone_GetGuide(LeftBox, 20.0f, Guides[Index].Zone, 30.0f);
        TEST_CHECK(run, (Box.x == Guides[Index].X) && (Box.y == Guides[Index].Y) && (Box.width == 30.0f) && (Box.height == 30.0f));
    }

    // Guides kept inside a small leaf as far as they fit.
    Rectangle Small = { .x = 0.0f, .y = 0.0f, .width = 40.0f, .height = 60.0f };
    Rectangle Guide = UIDockZone_GetGuide(Small, 20.0f, UIDockZone_Center, 30.0f);
    TEST_CHECK(run, (Guide.x == 5.0f) && (Guide.y == 25.0f) && (Guide.width == 30.0f) && (Guide.height == 30.0f));
    Guide = UIDockZone_GetGuide(Small, 20.0f, UIDockZone_Left, 30.0f);
    TEST_CHECK(run, Guide.x == 0.0f);
    Guide = UIDockZone_GetGuide(Small, 20.0f, UIDockZone_Right, 30.0f);
    TEST_CHECK(run, Guide.x == 10.0f);
    Guide = UIDockZone_GetGuide(Small, 20.0f, UIDockZone_Top, 30.0f);
    TEST_CHECK(run, Guide.y == 20.0f);
    Guide = UIDockZone_GetGuide(Small, 20.0f, UIDockZone_Bottom, 30.0f);
    TEST_CHECK(run, Guide.y == 30.0f);
    // A leaf shorter than its strip and a guide: the guides lie over the strip, inside the leaf, never below it.
    Rectangle Short = { .x = 10.0f, .y = 20.0f, .width = 100.0f, .height = 30.0f };
    for (int32_t Side = 0; Side < (int32_t)UIDockZone_Count; Side++)
    {
        Guide = UIDockZone_GetGuide(Short, 22.0f, (UIDockZone)Side, 28.0f);
        TEST_CHECK(run, (Guide.y >= Short.y) && ((Guide.y + Guide.height) <= (Short.y + Short.height)));
    }

    Rectangle Box = { .x = 0.0f, .y = 0.0f, .width = 400.0f, .height = 200.0f };
    Rectangle Preview = UIDockZone_GetPreview(Box, UIDockZone_Left, SPLITTER);
    TEST_CHECK(run, (Preview.x == 0.0f) && (Preview.width == 198.0f) && (Preview.height == 200.0f));
    Preview = UIDockZone_GetPreview(Box, UIDockZone_Bottom, SPLITTER);
    TEST_CHECK(run, (Preview.y == 102.0f) && (Preview.height == 98.0f) && (Preview.width == 400.0f));
    Preview = UIDockZone_GetPreview(Box, UIDockZone_Center, SPLITTER);
    TEST_CHECK(run, (Preview.x == 0.0f) && (Preview.width == 400.0f) && (Preview.height == 200.0f));
}

/* A tree broken by hand in every way the check knows is refused. */
static void BrokenTreesAreRefused(TestRun* run)
{
    UIDockTree Good;
    int32_t Right = UI_DOCK_NODE_NONE;
    int32_t Below = UI_DOCK_NODE_NONE;
    BuildThree(run, &Good, &Right, &Below);
    TEST_CHECK(run, UIDockTree_IsValid(&Good));
    int32_t Root = UIDockTree_GetRoot(&Good);

    UIDockTree Broken = Good;
    Broken._nodes[Right].Parent = Root; // Its parent is the inner split.
    TEST_CHECK(run, !UIDockTree_IsValid(&Broken));
    Broken = Good;
    Broken._nodes[Root].Children[1] = Broken._nodes[Root].Children[0];
    TEST_CHECK(run, !UIDockTree_IsValid(&Broken));
    Broken = Good;
    Broken._nodes[Root].Ratio = NAN;
    TEST_CHECK(run, !UIDockTree_IsValid(&Broken));
    Broken = Good;
    Broken._nodes[Root].Ratio = 1.5f;
    TEST_CHECK(run, !UIDockTree_IsValid(&Broken));
    Broken = Good;
    Broken._tabs[1].Item = Broken._tabs[0].Item;
    TEST_CHECK(run, !UIDockTree_IsValid(&Broken));
    Broken = Good;
    Broken._tabs[0].Item = UI_DOCK_ITEM_NONE;
    TEST_CHECK(run, !UIDockTree_IsValid(&Broken));
    Broken = Good;
    Broken._tabs[0].Leaf = Root; // A split holds no tabs.
    TEST_CHECK(run, !UIDockTree_IsValid(&Broken));
    Broken = Good;
    Broken._nodes[Below].Selected = 1U; // Another leaf's tab.
    TEST_CHECK(run, !UIDockTree_IsValid(&Broken));
    Broken = Good;
    Broken._nodes[Root].Parent = Right; // A loop.
    TEST_CHECK(run, !UIDockTree_IsValid(&Broken));
    Broken = Good;
    Broken._nodes[UI_DOCK_TREE_MAX_NODES - 1].Kind = UIDockNodeKind_Leaf; // Not reached from the root.
    TEST_CHECK(run, !UIDockTree_IsValid(&Broken));
    Broken = Good;
    Broken._root = 40;
    TEST_CHECK(run, !UIDockTree_IsValid(&Broken));
    Broken = Good;
    Broken._tabCount = UI_DOCK_TREE_MAX_TABS + 1U;
    TEST_CHECK(run, !UIDockTree_IsValid(&Broken));

    // One item twice in one leaf, its selection still its own.
    TEST_CHECK_SUCCESS(run, UIDockTree_AddTab(&Good, Below, 9U, 9U));
    TEST_CHECK_SUCCESS(run, UIDockTree_SelectTab(&Good, 3U));
    TEST_CHECK(run, UIDockTree_IsValid(&Good));
    Broken = Good;
    size_t Position = 9U;
    for (size_t Index = 0U; Index < Broken._tabCount; Index++)
    {
        Position = (Broken._tabs[Index].Item == 9U) ? Index : Position;
    }
    if (TEST_CHECK(run, Position < Broken._tabCount))
    {
        Broken._tabs[Position].Item = 3U;
        TEST_CHECK(run, !UIDockTree_IsValid(&Broken));
    }
    // A leaf with no tabs that says one is selected.
    Broken = Good;
    TEST_CHECK_SUCCESS(run, UIDockTree_RemoveTab(&Broken, 2U));
    TEST_CHECK(run, UIDockTree_IsValid(&Broken) && UIDockTree_HasEmptyLeaf(&Broken));
    Broken._nodes[Right].Selected = 1U;
    TEST_CHECK(run, !UIDockTree_IsValid(&Broken));

    // Deeper than allowed, spliced in by hand: the operations never build it, the check refuses it.
    UIDockTree Deep;
    UIDockTree_Construct(&Deep);
    TEST_CHECK_SUCCESS(run, UIDockTree_AddTab(&Deep, UI_DOCK_NODE_NONE, 1U, 0U));
    int32_t Deepest = UIDockTree_GetRoot(&Deep);
    for (uint64_t Item = 2U; Item < (uint64_t)(UI_DOCK_TREE_MAX_DEPTH + 2); Item++)
    {
        TEST_CHECK_SUCCESS(run, UIDockTree_SplitLeaf(&Deep, Deepest, UIDockZone_Right, Item, &Deepest));
    }
    TEST_CHECK(run, UIDockTree_IsValid(&Deep) && (UIDockTree_GetDepth(&Deep, Deepest) == UI_DOCK_TREE_MAX_DEPTH));
    int32_t Split = UI_DOCK_NODE_NONE;
    int32_t Extra = UI_DOCK_NODE_NONE;
    for (int32_t Index = 0; Index < (int32_t)UI_DOCK_TREE_MAX_NODES; Index++)
    {
        if (Deep._nodes[Index].Kind == UIDockNodeKind_Free)
        {
            Extra = (Split != UI_DOCK_NODE_NONE) && (Extra == UI_DOCK_NODE_NONE) ? Index : Extra;
            Split = (Split == UI_DOCK_NODE_NONE) ? Index : Split;
        }
    }
    if (TEST_CHECK(run, (Split != UI_DOCK_NODE_NONE) && (Extra != UI_DOCK_NODE_NONE)))
    {
        int32_t Above = Deep._nodes[Deepest].Parent;
        Deep._nodes[Above].Children[(Deep._nodes[Above].Children[0] == Deepest) ? 0 : 1] = Split;
        Deep._nodes[Split] = (UIDockNode){ .Kind = UIDockNodeKind_Split, .Parent = Above, .Split = UIDockSplit_Across, .Ratio = 0.5f,
            .Children = { Deepest, Extra }, .Selected = UI_DOCK_ITEM_NONE };
        Deep._nodes[Deepest].Parent = Split;
        Deep._nodes[Extra] = (UIDockNode){ .Kind = UIDockNodeKind_Leaf, .Parent = Split, .Split = UIDockSplit_Across, .Ratio = 0.0f,
            .Children = { UI_DOCK_NODE_NONE, UI_DOCK_NODE_NONE }, .Selected = 900U };
        Deep._tabs[Deep._tabCount] = (UIDockTab){ .Item = 900U, .Leaf = Extra };
        Deep._tabCount++;
        TEST_CHECK(run, !UIDockTree_IsValid(&Deep));
    }
}

/* Thousands of random operations, the tree whole after each, its leaves never empty once collapsed, every refusal changing nothing. */
static void RandomOperationsKeepTheTreeWhole(TestRun* run)
{
    uint64_t State = 99U;
    size_t Broken = 0U;
    size_t RefusalsThatChanged = 0U;
    for (size_t Round = 0U; Round < 40U; Round++)
    {
        UIDockTree Tree;
        UIDockTree_Construct(&Tree);
        for (size_t Step = 0U; Step < 400U; Step++)
        {
            UIDockTree Before = Tree;
            uint64_t Item = 1U + (NextRandom(&State) % 160U);
            int32_t Node = (int32_t)(NextRandom(&State) % (UI_DOCK_TREE_MAX_NODES + 2U)) - 1;
            Error Result = Error_CreateSuccess();
            switch (NextRandom(&State) % 7U)
            {
                case 0U:
                    Result = UIDockTree_AddTab(&Tree, UIDockTree_IsEmpty(&Tree) ? UI_DOCK_NODE_NONE : Node, Item, (size_t)(NextRandom(&State) % 6U));
                    break;
                case 1U:
                    Result = UIDockTree_RemoveTab(&Tree, Item);
                    break;
                case 2U:
                    Result = UIDockTree_MoveTab(&Tree, Item, Node, (size_t)(NextRandom(&State) % 6U));
                    break;
                case 3U:
                    Result = UIDockTree_SplitLeaf(&Tree, Node, (UIDockZone)(NextRandom(&State) % (uint64_t)UIDockZone_Count), Item, NULL);
                    break;
                case 4U:
                    Result = UIDockTree_SetRatio(&Tree, Node, ((float)(NextRandom(&State) % 1400U) / 1000.0f) - 0.2f);
                    break;
                case 5U:
                    Result = UIDockTree_SelectTab(&Tree, Item);
                    break;
                default:
                    Result = UIDockTree_ReplaceItem(&Tree, Item, 1U + (NextRandom(&State) % 160U));
                    break;
            }
            bool IsRefused = Result.Code != ErrorCode_Success;
            Error_Deconstruct(&Result);
            if (IsRefused && !Memory_IsEqual(&Before, &Tree, sizeof(Tree)))
            {
                RefusalsThatChanged++;
            }
            if ((NextRandom(&State) % 3U) == 0U)
            {
                UIDockTree_CollapseEmpty(&Tree);
                Broken += UIDockTree_HasEmptyLeaf(&Tree) ? 1U : 0U;
            }
            Broken += UIDockTree_IsValid(&Tree) ? 0U : 1U;
            UIDockArrangement Arrangement;
            UIDockTree_Arrange(&Tree, (Rectangle){ .x = 0.0f, .y = 0.0f, .width = 1200.0f, .height = 700.0f }, SPLITTER, NULL, NULL, &Arrangement);
            Broken += IsTiling(&Tree, &Arrangement) ? 0U : 1U;
        }
    }
    TEST_CHECK(run, Broken == 0U);
    TEST_CHECK(run, RefusalsThatChanged == 0U);
}


// Fields.
static const TestCase Cases[] =
{
    { "an_empty_tree_has_nothing", AnEmptyTreeHasNothing },
    { "tabs_are_added_in_place_selected_and_removed", TabsAreAddedInPlaceSelectedAndRemoved },
    { "tabs_stop_at_their_limit", TabsStopAtTheirLimit },
    { "tabs_move_along_a_leaf_and_to_another", TabsMoveAlongALeafAndToAnother },
    { "a_leaf_splits_on_each_side", ALeafSplitsOnEachSide },
    { "the_depth_and_leaf_limits_hold", TheDepthAndLeafLimitsHold },
    { "empty_leaves_collapse", EmptyLeavesCollapse },
    { "ratios_and_items_change_and_copy", RatiosAndItemsChangeAndCopy },
    { "splits_are_arranged_with_minimums", SplitsAreArrangedWithMinimums },
    { "random_trees_tile_the_area_at_many_sizes", RandomTreesTileTheAreaAtManySizes },
    { "lengths_below_zero_count_as_zero", LengthsBelowZeroCountAsZero },
    { "a_splitters_drag_sets_its_ratio", ASplittersDragSetsItsRatio },
    { "drop_zones_are_strips_and_guides", DropZonesAreStripsAndGuides },
    { "broken_trees_are_refused", BrokenTreesAreRefused },
    { "random_operations_keep_the_tree_whole", RandomOperationsKeepTheTreeWhole },
};


// Public functions.
void UIDockTreeTests_Run(TestRun* run)
{
    TestRun_RunSuite(run, "UIDockTree", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
