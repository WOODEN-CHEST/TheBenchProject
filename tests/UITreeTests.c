#include "TestSuites.h"
#include "TestUI.h"
#include "UIContextMenu.h"
#include "UIDragDrop.h"
#include "UIKit.h"
#include "UIListRow.h"
#include "UITree.h"
#include "UIWidgetFactory.h"
#include "wr/WRMemory.h"
#include "wr/WRNumber.h"
#include "wr/WRUserData.h"
#include <string.h>


// Macros.
/** The fixture's look: 20 pixel rows inside a one pixel padding, rows padded by 4, levels 16 apart, 12 pixel cells, a 10 pixel bar. */
#define TREE_THEME \
    "{\"name\": \"trees\", \"font\": \"mono\", \"text_size\": 16, \"elements\": {" \
    "\"scrollbar\": {\"thickness\": 10, \"min_thumb_length\": 10, \"thumb_inset\": 0, \"track\": {\"kind\": \"flat\"}, \"thumb\": {\"kind\": \"flat\"}}," \
    "\"tree\": {\"padding\": [1, 1], \"row_height\": 20, \"row_padding\": 4, \"indent\": 16, \"cell_size\": 12, \"insertion_thickness\": 2," \
    "\"background\": {\"kind\": \"flat\"}, \"row\": {\"kind\": \"flat\"}, \"current\": {\"kind\": \"none\"}, \"text\": {\"normal\": \"#e0e0e0\"}," \
    "\"expander\": {\"normal\": \"#ffffff\"}, \"cell\": {\"kind\": \"flat\"}, \"cell_text\": {\"normal\": \"#e0e0e0\"}, \"insertion\": {\"kind\": \"flat\"}}}}"
#define TREE_X 100.0f
#define TREE_Y 100.0f
#define TREE_WIDTH 300.0f
#define TREE_HEIGHT 202.0f
#define ROW 20.0f
#define ROWS_X (TREE_X + 1.0f)
#define ROWS_Y (TREE_Y + 1.0f)
#define ROWS_IN_VIEW ((size_t)10)
/** The most nodes the fixture's data remembers as expanded or removed. */
#define MAX_MARKED ((size_t)64)
/** A node's id holds its path: four bits a level, each its child's index plus one, and a high bit so ids lie past what a double holds. */
#define ID_BASE (((uint64_t)1U) << 62)


// Types.
/* The caller's data: a tree of Fanout children a node to MaxDepth levels (each top-level node's path one digit), the nodes it keeps
 * expanded and the ones removed, and how many nodes were written. */
typedef struct NodesStruct
{
    size_t TopCount;
    size_t Fanout;
    size_t MaxDepth;
    uint64_t Expanded[MAX_MARKED];
    size_t ExpandedCount;
    uint64_t Removed[MAX_MARKED];
    size_t RemovedCount;
    size_t Written;
    bool IsChain;
} Nodes;


// Static functions: the data.
static size_t DepthOf(uint64_t node)
{
    size_t Depth = 0U;
    for (uint64_t Path = node & ~ID_BASE; Path != 0U; Path >>= 4U)
    {
        Depth++;
    }
    return Depth;
}

static uint64_t ChildOf(bool isRoot, uint64_t node, size_t index)
{
    uint64_t Path = isRoot ? 0U : (node & ~ID_BASE);
    return ID_BASE | (Path << 4U) | ((uint64_t)index + 1U);
}

static bool IsMarked(const uint64_t* marks, size_t count, uint64_t node)
{
    for (size_t Index = 0U; Index < count; Index++)
    {
        if (marks[Index] == node)
        {
            return true;
        }
    }
    return false;
}

static size_t GetChildCount(UIScreen* screen, uint64_t treeId, const UserData* userData, bool isRoot, uint64_t node)
{
    (void)screen;
    (void)treeId;
    const Nodes* Data = UserData_GetPointer(userData);
    if (isRoot)
    {
        return Data->TopCount;
    }
    if (Data->IsChain)
    {
        return 1U; // A chain without end: each node has one child.
    }
    return (DepthOf(node) < Data->MaxDepth) ? Data->Fanout : 0U;
}

static uint64_t GetChild(UIScreen* screen, uint64_t treeId, const UserData* userData, bool isRoot, uint64_t node, size_t index)
{
    (void)screen;
    (void)treeId;
    const Nodes* Data = UserData_GetPointer(userData);
    if (Data->IsChain && !isRoot)
    {
        return node + 1U; // Ids a chain apart; never 0.
    }
    return ChildOf(isRoot, node, index);
}

static bool IsExpanded(UIScreen* screen, uint64_t treeId, const UserData* userData, uint64_t node)
{
    (void)screen;
    (void)treeId;
    const Nodes* Data = UserData_GetPointer(userData);
    return Data->IsChain || IsMarked(Data->Expanded, Data->ExpandedCount, node);
}

static void SetExpanded(UIScreen* screen, uint64_t treeId, const UserData* userData, uint64_t node, bool isExpanded)
{
    (void)screen;
    (void)treeId;
    Nodes* Data = UserData_GetPointer(userData);
    for (size_t Index = 0U; Index < Data->ExpandedCount; Index++)
    {
        if (Data->Expanded[Index] == node)
        {
            if (!isExpanded)
            {
                Data->Expanded[Index] = Data->Expanded[Data->ExpandedCount - 1U];
                Data->ExpandedCount--;
            }
            return;
        }
    }
    if (isExpanded && (Data->ExpandedCount < MAX_MARKED))
    {
        Data->Expanded[Data->ExpandedCount++] = node;
    }
}

static Error WriteNode(UIScreen* screen, uint64_t treeId, const UserData* userData, uint64_t node, UIListRowData* outRow)
{
    (void)screen;
    (void)treeId;
    Nodes* Data = UserData_GetPointer(userData);
    Data->Written++;
    GenericBuffer_AppendString(outRow->Text, u8"Node ");
    return Number_UInt64ToString(node & ~ID_BASE, 16, false, outRow->Text);
}

static bool HasNode(UIScreen* screen, uint64_t treeId, const UserData* userData, uint64_t node)
{
    (void)screen;
    (void)treeId;
    const Nodes* Data = UserData_GetPointer(userData);
    return !IsMarked(Data->Removed, Data->RemovedCount, node);
}

static const UITreeProvider Provider =
{
    .GetChildCount = GetChildCount,
    .GetChild = GetChild,
    .IsExpanded = IsExpanded,
    .SetExpanded = SetExpanded,
    .WriteNode = WriteNode,
    .HasNode = HasNode
};


// Static functions: helpers.
static bool Construct(TestRun* run, TestUIFixture* fixture)
{
    return TestUI_Construct(run, fixture) && TestUI_SetTheme(run, fixture, TREE_THEME);
}

static uint64_t AddTree(TestRun* run, TestUIFixture* fixture, Nodes* nodes, UISelectionMode mode)
{
    UITreeOptions Options = UITreeOptions_Create();
    Options.SelectionMode = mode;
    Options.Provider = &Provider;
    Options.ProviderData = UserData_FromPointer(nodes);
    Options.Element.HasPlacement = true;
    Options.Element.Placement = UIPlacement_FromTopLeft(TREE_X, TREE_Y, TREE_WIDTH, TREE_HEIGHT);
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIKit_CreateTree(&fixture->Kit, &fixture->Screen, WIDGET_ID_INVALID, &Options, &Id));
    TestUI_Ticks(run, fixture, 3U);
    return Id;
}

static uint64_t Top(size_t index)
{
    return ChildOf(true, 0U, index);
}

static float RowY(size_t row)
{
    return ROWS_Y + ((float)row * ROW) + (ROW * 0.5f);
}

static void ClickRow(TestRun* run, TestUIFixture* fixture, size_t row)
{
    static float Across = 0.0f;
    Across = (Across >= 120.0f) ? 0.0f : (Across + 20.0f);
    TestUI_Click(run, fixture, TREE_X + 120.0f + Across, RowY(row));
}

/* A click on a row's expander: at its depth's indent, after the padding. */
static void ClickExpander(TestRun* run, TestUIFixture* fixture, size_t row, size_t depth)
{
    TestUI_Click(run, fixture, ROWS_X + 4.0f + ((float)depth * 16.0f) + 8.0f, RowY(row));
}

static size_t RowCount(TestRun* run, TestUIFixture* fixture, uint64_t id)
{
    size_t Count = 0U;
    TEST_CHECK_SUCCESS(run, UITree_GetRowCount(&fixture->Screen, id, &Count));
    return Count;
}

static UITreeRow RowAt(TestRun* run, TestUIFixture* fixture, uint64_t id, size_t index)
{
    UITreeRow Row = { .Node = 0U, .Depth = 0U, .HasChildren = false, .IsExpanded = false };
    TEST_CHECK_SUCCESS(run, UITree_GetRow(&fixture->Screen, id, index, &Row));
    return Row;
}

static bool IsSelected(TestRun* run, TestUIFixture* fixture, uint64_t id, uint64_t node)
{
    bool Is = false;
    TEST_CHECK_SUCCESS(run, UITree_IsSelected(&fixture->Screen, id, node, &Is));
    return Is;
}

static size_t SelectedCount(TestRun* run, TestUIFixture* fixture, uint64_t id)
{
    size_t Count = 0U;
    TEST_CHECK_SUCCESS(run, UITree_GetSelectedCount(&fixture->Screen, id, &Count));
    return Count;
}

static bool HasCurrent(TestRun* run, TestUIFixture* fixture, uint64_t id, uint64_t node)
{
    bool Has = false;
    uint64_t Current = 0U;
    TEST_CHECK_SUCCESS(run, UITree_GetCurrent(&fixture->Screen, id, &Has, &Current));
    return Has && (Current == node);
}

static size_t CountRowWidgets(TestUIFixture* fixture, uint64_t treeId)
{
    Widget* Tree = NULL;
    if (!UIScreen_TryGetWidgetById(&fixture->Screen, treeId, &Tree) || (Widget_GetSubWidgetCount(Tree) == 0U))
    {
        return 0U;
    }
    return Widget_GetSubWidgetCount(Widget_GetSubWidgetAt(Tree, 0U));
}

static bool IsNodeEvent(const TestUIEvents* events, size_t index, uint64_t id, UIElementEvent event, uint64_t node)
{
    return (index < events->Count) && (events->Events[index].WidgetId == id) && (events->Events[index].WidgetEventId == (int32_t)event)
        && events->Events[index].HasValue && (events->Events[index].Value.Type == UIPropertyType_UInt64)
        && (events->Events[index].Value.Data.UInt64Value == node);
}

static void TapWith(TestRun* run, TestUIFixture* fixture, GameKey modifier, GameKey key)
{
    TestUI_Press(fixture, modifier);
    TestUI_Tick(run, fixture);
    TestUI_Tap(run, fixture, key);
    TestUI_Release(fixture, modifier);
    TestUI_Tick(run, fixture);
}


// Tests.
static void TheWalkShowsWhatIsExpanded(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        Nodes Data = { .TopCount = 3U, .Fanout = 2U, .MaxDepth = 3U };
        Data.Expanded[Data.ExpandedCount++] = Top(1U);
        Data.Expanded[Data.ExpandedCount++] = ChildOf(false, Top(1U), 0U);
        uint64_t Id = AddTree(run, &Fixture, &Data, UISelectionMode_Multiple);
        // Top 0; top 1 open with its first child open (two leaves under it) and its second shut; top 2.
        TEST_CHECK(run, RowCount(run, &Fixture, Id) == 7U);
        UITreeRow Row = RowAt(run, &Fixture, Id, 1U);
        TEST_CHECK(run, (Row.Node == Top(1U)) && (Row.Depth == 0U) && Row.HasChildren && Row.IsExpanded);
        Row = RowAt(run, &Fixture, Id, 2U);
        TEST_CHECK(run, (Row.Node == ChildOf(false, Top(1U), 0U)) && (Row.Depth == 1U) && Row.IsExpanded);
        Row = RowAt(run, &Fixture, Id, 3U);
        TEST_CHECK(run, (Row.Depth == 2U) && !Row.HasChildren && !Row.IsExpanded);
        Row = RowAt(run, &Fixture, Id, 5U);
        TEST_CHECK(run, (Row.Node == ChildOf(false, Top(1U), 1U)) && (Row.Depth == 1U) && Row.HasChildren && !Row.IsExpanded);
        TEST_CHECK(run, RowAt(run, &Fixture, Id, 6U).Node == Top(2U));
        // Expanding by code shows the children at the next update.
        TEST_CHECK_SUCCESS(run, UITree_SetExpanded(&Fixture.Screen, Id, Top(2U), true));
        TEST_CHECK(run, RowCount(run, &Fixture, Id) == 7U);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, RowCount(run, &Fixture, Id) == 9U);
        // A chain without end stops at the deepest level shown.
        Nodes Chain = { .TopCount = 1U, .IsChain = true };
        UserData ChainData = UserData_FromPointer(&Chain);
        TEST_CHECK_SUCCESS(run, UITree_SetProvider(&Fixture.Screen, Id, &Provider, &ChainData));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, RowCount(run, &Fixture, Id) == (size_t)UI_TREE_MAX_DEPTH);
        TEST_CHECK(run, RowAt(run, &Fixture, Id, (size_t)UI_TREE_MAX_DEPTH - 1U).Depth == (UI_TREE_MAX_DEPTH - 1U));
        // A provider missing a hook is refused.
        UITreeProvider Broken = Provider;
        Broken.HasNode = NULL;
        TEST_CHECK_ERROR(run, UITree_SetProvider(&Fixture.Screen, Id, &Broken, NULL), ErrorCode_IllegalArgument);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void AHundredThousandNodesCostTheRowsInView(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        Nodes Data = { .TopCount = 100000U, .Fanout = 0U, .MaxDepth = 0U };
        size_t Before = UIScreen_GetWidgetCount(&Fixture.Screen);
        uint64_t Id = AddTree(run, &Fixture, &Data, UISelectionMode_Multiple);
        TEST_CHECK(run, RowCount(run, &Fixture, Id) == 100000U);
        TEST_CHECK(run, CountRowWidgets(&Fixture, Id) <= (ROWS_IN_VIEW + 1U));
        TEST_CHECK(run, (UIScreen_GetWidgetCount(&Fixture.Screen) - Before) <= (ROWS_IN_VIEW + 4U));
        TEST_CHECK(run, Data.Written <= (ROWS_IN_VIEW + 1U) * 2U);
        // Idle, nothing is walked or written again, and nothing is allocated.
        ClickRow(run, &Fixture, 2U);
        TestUI_Ticks(run, &Fixture, 5U);
        size_t Written = Data.Written;
        size_t Allocations = Memory_GetTotalAllocationCount();
        TestUI_Ticks(run, &Fixture, 30U);
        TEST_CHECK(run, (Data.Written == Written) && (Memory_GetTotalAllocationCount() == Allocations));
        // To the end with the keys: the view follows and the widgets stay about as many.
        TestUI_Tap(run, &Fixture, GameKey_End);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, HasCurrent(run, &Fixture, Id, Top(99999U)) && IsSelected(run, &Fixture, Id, Top(99999U)));
        TEST_CHECK(run, CountRowWidgets(&Fixture, Id) <= (ROWS_IN_VIEW + 1U));
        // Shift and Home select the whole tree in one run; select-all too.
        TapWith(run, &Fixture, GameKey_LeftShift, GameKey_Home);
        TEST_CHECK(run, SelectedCount(run, &Fixture, Id) == 100000U);
        TestUI_Tap(run, &Fixture, GameKey_Space);
        TEST_CHECK(run, SelectedCount(run, &Fixture, Id) == 1U);
        TapWith(run, &Fixture, GameKey_LeftControl, GameKey_A);
        TEST_CHECK(run, SelectedCount(run, &Fixture, Id) == 100000U);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void TheExpanderAndTheSideKeysOpenAndClose(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        Nodes Data = { .TopCount = 2U, .Fanout = 3U, .MaxDepth = 3U };
        uint64_t Id = AddTree(run, &Fixture, &Data, UISelectionMode_Multiple);
        TEST_CHECK(run, RowCount(run, &Fixture, Id) == 2U);
        // A press on the expander opens the node at once, and selects nothing.
        ClickExpander(run, &Fixture, 0U, 0U);
        TEST_CHECK(run, (RowCount(run, &Fixture, Id) == 5U) && (SelectedCount(run, &Fixture, Id) == 0U));
        ClickExpander(run, &Fixture, 0U, 0U);
        TEST_CHECK(run, RowCount(run, &Fixture, Id) == 2U);
        // Right opens the current node, then goes to its first child; Left goes to the parent, then closes it.
        ClickRow(run, &Fixture, 0U);
        TestUI_Tap(run, &Fixture, GameKey_Right);
        TEST_CHECK(run, (RowCount(run, &Fixture, Id) == 5U) && HasCurrent(run, &Fixture, Id, Top(0U)));
        TestUI_Tap(run, &Fixture, GameKey_Right);
        uint64_t FirstChild = ChildOf(false, Top(0U), 0U);
        TEST_CHECK(run, HasCurrent(run, &Fixture, Id, FirstChild) && IsSelected(run, &Fixture, Id, FirstChild));
        TestUI_Tap(run, &Fixture, GameKey_Right); // A child with its own children: it opens.
        TEST_CHECK(run, RowCount(run, &Fixture, Id) == 8U);
        TestUI_Tap(run, &Fixture, GameKey_Left); // Open: it closes.
        TEST_CHECK(run, (RowCount(run, &Fixture, Id) == 5U) && HasCurrent(run, &Fixture, Id, FirstChild));
        TestUI_Tap(run, &Fixture, GameKey_Left); // Closed: to the parent.
        TEST_CHECK(run, HasCurrent(run, &Fixture, Id, Top(0U)));
        TestUI_Tap(run, &Fixture, GameKey_Left);
        TEST_CHECK(run, RowCount(run, &Fixture, Id) == 2U);
        // Closing a node whose subtree holds the current row makes the node current.
        TestUI_Tap(run, &Fixture, GameKey_Right);
        TestUI_Tap(run, &Fixture, GameKey_Down);
        TestUI_Tap(run, &Fixture, GameKey_Down);
        TEST_CHECK(run, HasCurrent(run, &Fixture, Id, ChildOf(false, Top(0U), 1U)));
        ClickExpander(run, &Fixture, 0U, 0U);
        TEST_CHECK(run, (RowCount(run, &Fixture, Id) == 2U) && HasCurrent(run, &Fixture, Id, Top(0U)));
        // The caller keeps the state: a tree made again over the same data shows the same.
        TEST_CHECK_SUCCESS(run, UITree_SetExpanded(&Fixture.Screen, Id, Top(1U), true));
        uint64_t Again = AddTree(run, &Fixture, &Data, UISelectionMode_Multiple);
        TEST_CHECK(run, RowCount(run, &Fixture, Again) == 5U);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void SelectedNodesOutliveCollapsingButNotRemoval(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        Nodes Data = { .TopCount = 3U, .Fanout = 3U, .MaxDepth = 2U };
        Data.Expanded[Data.ExpandedCount++] = Top(0U);
        uint64_t Id = AddTree(run, &Fixture, &Data, UISelectionMode_Multiple);
        // Rows: top 0, its three children, top 1, top 2. Shift spans rows across levels.
        ClickRow(run, &Fixture, 1U);
        TestUI_Press(&Fixture, GameKey_LeftShift);
        TestUI_Tick(run, &Fixture);
        ClickRow(run, &Fixture, 4U);
        TestUI_Release(&Fixture, GameKey_LeftShift);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, SelectedCount(run, &Fixture, Id) == 4U);
        TEST_CHECK(run, IsSelected(run, &Fixture, Id, ChildOf(false, Top(0U), 2U)) && IsSelected(run, &Fixture, Id, Top(1U)));
        TEST_CHECK(run, !IsSelected(run, &Fixture, Id, Top(0U)));
        // Collapsed away, they stay selected.
        ClickExpander(run, &Fixture, 0U, 0U);
        TEST_CHECK(run, (RowCount(run, &Fixture, Id) == 3U) && (SelectedCount(run, &Fixture, Id) == 4U));
        // Removed and refreshed, they drop out; no event (code's change).
        Events.Count = 0U;
        Data.Removed[Data.RemovedCount++] = ChildOf(false, Top(0U), 1U);
        Data.Removed[Data.RemovedCount++] = Top(1U);
        TEST_CHECK_SUCCESS(run, UITree_Refresh(&Fixture.Screen, Id));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (SelectedCount(run, &Fixture, Id) == 2U) && (Events.Count == 0U));
        uint64_t First = 0U;
        uint64_t Second = 0U;
        TEST_CHECK_SUCCESS(run, UITree_GetSelectedAt(&Fixture.Screen, Id, 0U, &First));
        TEST_CHECK_SUCCESS(run, UITree_GetSelectedAt(&Fixture.Screen, Id, 1U, &Second));
        TEST_CHECK(run, (First < Second) && (First == ChildOf(false, Top(0U), 0U)) && (Second == ChildOf(false, Top(0U), 2U)));
        TEST_CHECK_ERROR(run, UITree_GetSelectedAt(&Fixture.Screen, Id, 2U, &First), ErrorCode_ArgumentOutOfRange);
        // Ctrl toggles; the modes keep to their counts.
        ClickRow(run, &Fixture, 2U);
        TestUI_Press(&Fixture, GameKey_LeftControl);
        TestUI_Tick(run, &Fixture);
        ClickRow(run, &Fixture, 0U);
        ClickRow(run, &Fixture, 2U);
        TestUI_Release(&Fixture, GameKey_LeftControl);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (SelectedCount(run, &Fixture, Id) == 1U) && IsSelected(run, &Fixture, Id, Top(0U)));
        TEST_CHECK_SUCCESS(run, UITree_SetSelected(&Fixture.Screen, Id, Top(2U), true));
        TEST_CHECK_SUCCESS(run, UITree_SetSelectionMode(&Fixture.Screen, Id, UISelectionMode_Single));
        TEST_CHECK(run, SelectedCount(run, &Fixture, Id) == 1U);
        TEST_CHECK_SUCCESS(run, UITree_SetSelectionMode(&Fixture.Screen, Id, UISelectionMode_None));
        TEST_CHECK(run, SelectedCount(run, &Fixture, Id) == 0U);
        TEST_CHECK_ERROR(run, UITree_SetSelected(&Fixture.Screen, Id, Top(2U), true), ErrorCode_InvalidOperation);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A context menu provider that remembers which node it was asked about. */
static Error RememberMenuNode(UIScreen* screen, const UIContextMenuRequest* request, const UserData* userData)
{
    uint64_t* Node = UserData_GetPointer(userData);
    bool Has = false;
    return UITree_GetItemAt(screen, request->ProviderWidgetId, request->ScreenPosition, &Has, Node);
}

static size_t CountCommits(const TestUIEvents* events, uint64_t id)
{
    size_t Count = 0U;
    for (size_t Index = 0U; Index < events->Count; Index++)
    {
        Count += ((events->Events[Index].WidgetId == id) && (events->Events[Index].WidgetEventId == (int32_t)UIElementEvent_Committed)) ? 1U : 0U;
    }
    return Count;
}

/* The text the row widget showing a tree's row shows, or NULL when none shows it. */
static const char* ShownText(TestUIFixture* fixture, uint64_t treeId, size_t row)
{
    Widget* Tree = NULL;
    uint64_t RowId = WIDGET_ID_INVALID;
    const unsigned char* Text = NULL;
    bool IsBound = false;
    if (!UIScreen_TryGetWidgetById(&fixture->Screen, treeId, &Tree)
        || !UIRowRecycler_FindWidget(&((const UITreeLookBase*)(const void*)Tree)->Tree._recycler, row, &RowId)
        || !UIListRow_Peek(&fixture->Screen, RowId, &Text, &IsBound, NULL) || !IsBound)
    {
        return NULL;
    }
    return (const char*)Text;
}

static void FoldsAndDataChangesKeepTheCurrentRow(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        Nodes Data = { .TopCount = 3U, .Fanout = 3U, .MaxDepth = 2U };
        Data.Expanded[Data.ExpandedCount++] = Top(0U);
        uint64_t Id = AddTree(run, &Fixture, &Data, UISelectionMode_Multiple);
        // Rows: top 0, its three children (leaves), top 1, top 2. Left from the second child goes to the parent, past its sibling.
        ClickRow(run, &Fixture, 2U);
        TestUI_Tap(run, &Fixture, GameKey_Left);
        TEST_CHECK(run, HasCurrent(run, &Fixture, Id, Top(0U)));
        // Folding a node keeps a current row that lies past its subtree.
        ClickRow(run, &Fixture, 4U);
        ClickExpander(run, &Fixture, 0U, 0U);
        TEST_CHECK(run, (RowCount(run, &Fixture, Id) == 3U) && HasCurrent(run, &Fixture, Id, Top(1U)));
        // Its rows show the nodes now at their places, written anew: the second row is top 1, no longer the first child.
        const char* Folded = ShownText(&Fixture, Id, 1U);
        TEST_CHECK(run, (Folded != NULL) && (strcmp(Folded, "Node 2") == 0));
        // Ctrl and Shift over rows already selected change nothing and post nothing.
        ClickRow(run, &Fixture, 0U);
        TestUI_Press(&Fixture, GameKey_LeftShift);
        TestUI_Tick(run, &Fixture);
        ClickRow(run, &Fixture, 2U);
        TestUI_Release(&Fixture, GameKey_LeftShift);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, SelectedCount(run, &Fixture, Id) == 3U);
        Events.Count = 0U;
        TestUI_Press(&Fixture, GameKey_LeftControl);
        TestUI_Press(&Fixture, GameKey_LeftShift);
        TestUI_Tick(run, &Fixture);
        ClickRow(run, &Fixture, 1U);
        TestUI_Release(&Fixture, GameKey_LeftControl);
        TestUI_Release(&Fixture, GameKey_LeftShift);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (SelectedCount(run, &Fixture, Id) == 3U) && (CountCommits(&Events, Id) == 0U));
        // Unfolded again: the rows show the nodes now at their places, written anew.
        ClickExpander(run, &Fixture, 0U, 0U);
        TEST_CHECK(run, RowCount(run, &Fixture, Id) == 6U);
        const char* Second = ShownText(&Fixture, Id, 1U);
        TEST_CHECK(run, (Second != NULL) && (strcmp(Second, "Node 11") == 0));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* The current node no longer shown after the data changed: the row now at its place becomes current, or the last row. */
static void AGoneCurrentNodeGivesItsPlaceToItsRow(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        Nodes Data = { .TopCount = 4U, .Fanout = 3U, .MaxDepth = 2U };
        uint64_t Id = AddTree(run, &Fixture, &Data, UISelectionMode_Multiple);
        ClickRow(run, &Fixture, 3U); // The last of four.
        TEST_CHECK(run, HasCurrent(run, &Fixture, Id, Top(3U)));
        // The data shrinks to two top-level nodes: the current row's place is past the end, so the last row takes it.
        Data.TopCount = 2U;
        TEST_CHECK_SUCCESS(run, UITree_Refresh(&Fixture.Screen, Id));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, HasCurrent(run, &Fixture, Id, Top(1U)));
        // Four again, the first unfolded by the data, the current its second child (the third row); folded by the data: the node now
        // third is current.
        Data.TopCount = 4U;
        Data.Expanded[Data.ExpandedCount++] = Top(0U);
        TEST_CHECK_SUCCESS(run, UITree_Refresh(&Fixture.Screen, Id));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UITree_SetCurrent(&Fixture.Screen, Id, ChildOf(false, Top(0U), 1U)));
        Data.ExpandedCount = 0U;
        TEST_CHECK_SUCCESS(run, UITree_Refresh(&Fixture.Screen, Id));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, HasCurrent(run, &Fixture, Id, Top(2U)));
    }
    TestUI_Deconstruct(run, &Fixture);
}


static void AnExpandersPressDragsNothing(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        Nodes Data = { .TopCount = 3U, .Fanout = 3U, .MaxDepth = 2U };
        uint64_t Id = AddTree(run, &Fixture, &Data, UISelectionMode_Multiple);
        TEST_CHECK_SUCCESS(run, UITree_SetDragSource(&Fixture.Screen, Id, true, 1U));
        TEST_CHECK_SUCCESS(run, UITree_SetSelected(&Fixture.Screen, Id, Top(0U), true));
        TEST_CHECK_SUCCESS(run, UITree_SetSelected(&Fixture.Screen, Id, Top(1U), true));
        TestUI_Tick(run, &Fixture);
        // A press on a selected node's expander opens it at once; moved on past the drag distance, it drags nothing.
        float ExpanderX = ROWS_X + 4.0f + 8.0f;
        TestUI_MovePointer(&Fixture, ExpanderX, RowY(0U));
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, RowCount(run, &Fixture, Id) == 6U);
        TestUI_MovePointer(&Fixture, ExpanderX + 60.0f, RowY(0U) + 10.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, !UIDragDrop_IsDragging(&Fixture.Screen));
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        // A press on the selected node's row itself drags them.
        TestUI_MovePointer(&Fixture, TREE_X + 150.0f, RowY(0U));
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_MovePointer(&Fixture, TREE_X + 150.0f, RowY(0U) + 10.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, UIDragDrop_IsDragging(&Fixture.Screen));
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        // A tree with a toggle cell: a press on a selected node's cell drags nothing either.
        Widget* Old = NULL;
        TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Old) && TEST_CHECK_SUCCESS(run, Widget_Destroy(Old)));
        UITreeOptions Options = UITreeOptions_Create();
        Options.SelectionMode = UISelectionMode_Multiple;
        Options.Provider = &Provider;
        Options.ProviderData = UserData_FromPointer(&Data);
        Options.CellCount = 1U;
        Options.CellIcons[0] = UIElementIcon_FromLetter((uint32_t)'V');
        Options.IsDragSource = true;
        Options.DragKind = 1U;
        Options.Element.HasPlacement = true;
        Options.Element.Placement = UIPlacement_FromTopLeft(TREE_X, TREE_Y, TREE_WIDTH, TREE_HEIGHT);
        uint64_t Celled = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateTree(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Celled));
        TestUI_Ticks(run, &Fixture, 3U);
        TEST_CHECK_SUCCESS(run, UITree_SetSelected(&Fixture.Screen, Celled, Top(0U), true));
        // Six rows fit: no bar, so the rows are 298 wide and the cell stands 4 from their right edge, 12 wide.
        float CellX = ROWS_X + 298.0f - 4.0f - 6.0f;
        TestUI_MovePointer(&Fixture, CellX - 3.0f, RowY(0U) - 3.0f);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_MovePointer(&Fixture, CellX + 3.0f, RowY(0U) + 3.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, !UIDragDrop_IsDragging(&Fixture.Screen));
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
    }
    TestUI_Deconstruct(run, &Fixture);
}


static void EventsNameTheirNodeWholly(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        Events.IsRecordingInputEvents = true;
        Nodes Data = { .TopCount = 5U, .Fanout = 0U, .MaxDepth = 0U };
        uint64_t Id = AddTree(run, &Fixture, &Data, UISelectionMode_Multiple);
        // The ids lie past what a double holds exactly: the events carry them whole.
        ClickRow(run, &Fixture, 3U);
        Events.Count = 0U;
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, (Events.Count == 1U) && IsNodeEvent(&Events, 0U, Id, UIElementEvent_Activated, Top(3U)));
        Events.Count = 0U;
        TestUI_Click(run, &Fixture, TREE_X + 150.0f, RowY(1U));
        TestUI_Click(run, &Fixture, TREE_X + 150.0f, RowY(1U));
        bool IsDoubleClicked = false;
        bool IsActivated = false;
        for (size_t Index = 0U; Index < Events.Count; Index++)
        {
            IsDoubleClicked = IsDoubleClicked || IsNodeEvent(&Events, Index, Id, UIElementEvent_DoubleClicked, Top(1U));
            IsActivated = IsActivated || IsNodeEvent(&Events, Index, Id, UIElementEvent_Activated, Top(1U));
        }
        TEST_CHECK(run, IsDoubleClicked && IsActivated);
        uint64_t MenuNode = 0U;
        UserData Remember = UserData_FromPointer(&MenuNode);
        TEST_CHECK_SUCCESS(run, UIContextMenu_SetProvider(&Fixture.Screen, Id, RememberMenuNode, &Remember));
        Events.Count = 0U;
        TestUI_MovePointer(&Fixture, TREE_X + 150.0f, RowY(4U));
        TestUI_Press(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, MenuNode == Top(4U));
        bool IsRequested = false;
        for (size_t Index = 0U; Index < Events.Count; Index++)
        {
            IsRequested = IsRequested || IsNodeEvent(&Events, Index, Id, UIElementEvent_ContextRequested, Top(4U));
        }
        TEST_CHECK(run, IsRequested && IsSelected(run, &Fixture, Id, Top(4U)));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void WhereADropWouldGo(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        Nodes Data = { .TopCount = 4U, .Fanout = 0U, .MaxDepth = 0U };
        uint64_t Id = AddTree(run, &Fixture, &Data, UISelectionMode_Multiple);
        uint64_t Node = 0U;
        UITreeDropEdge Edge = UITreeDropEdge_None;
        Vector2 At = { .x = (TREE_X + 100.0f) / TEST_UI_SURFACE_WIDTH, .y = (ROWS_Y + ROW + 2.0f) / TEST_UI_SURFACE_HEIGHT };
        TEST_CHECK_SUCCESS(run, UITree_GetDropPlace(&Fixture.Screen, Id, At, &Node, &Edge));
        TEST_CHECK(run, (Node == Top(1U)) && (Edge == UITreeDropEdge_Above));
        At.y = (ROWS_Y + ROW + 10.0f) / TEST_UI_SURFACE_HEIGHT;
        TEST_CHECK_SUCCESS(run, UITree_GetDropPlace(&Fixture.Screen, Id, At, &Node, &Edge));
        TEST_CHECK(run, (Node == Top(1U)) && (Edge == UITreeDropEdge_Into));
        At.y = (ROWS_Y + ROW + 18.0f) / TEST_UI_SURFACE_HEIGHT;
        TEST_CHECK_SUCCESS(run, UITree_GetDropPlace(&Fixture.Screen, Id, At, &Node, &Edge));
        TEST_CHECK(run, (Node == Top(1U)) && (Edge == UITreeDropEdge_Below));
        At.y = (ROWS_Y + 150.0f) / TEST_UI_SURFACE_HEIGHT;
        TEST_CHECK_SUCCESS(run, UITree_GetDropPlace(&Fixture.Screen, Id, At, &Node, &Edge));
        TEST_CHECK(run, (Node == Top(3U)) && (Edge == UITreeDropEdge_Below));
        Data.TopCount = 0U;
        TEST_CHECK_SUCCESS(run, UITree_Refresh(&Fixture.Screen, Id));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UITree_GetDropPlace(&Fixture.Screen, Id, At, &Node, &Edge));
        TEST_CHECK(run, Edge == UITreeDropEdge_None);
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Fields.
static const TestCase Cases[] =
{
    { "folds_and_data_changes_keep_the_current_row", FoldsAndDataChangesKeepTheCurrentRow },
    { "a_gone_current_node_gives_its_place_to_its_row", AGoneCurrentNodeGivesItsPlaceToItsRow },
    { "an_expanders_press_drags_nothing", AnExpandersPressDragsNothing },
    { "the_walk_shows_what_is_expanded", TheWalkShowsWhatIsExpanded },
    { "a_hundred_thousand_nodes_cost_the_rows_in_view", AHundredThousandNodesCostTheRowsInView },
    { "the_expander_and_the_side_keys_open_and_close", TheExpanderAndTheSideKeysOpenAndClose },
    { "selected_nodes_outlive_collapsing_but_not_removal", SelectedNodesOutliveCollapsingButNotRemoval },
    { "events_name_their_node_wholly", EventsNameTheirNodeWholly },
    { "where_a_drop_would_go", WhereADropWouldGo },
};


// Public functions.
void UITreeTests_Run(TestRun* run)
{
    TestRun_RunSuite(run, "UITree", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
