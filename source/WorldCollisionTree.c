#include <float.h>
#include <math.h>
#include "WorldCollisionTree.h"

/*
 * The dynamic bounding volume tree (source/WorldCollisionTree.h), after Box2D's b2DynamicTree: surface-area-guided
 * insertion, AVL rotations on the way back up, a free list of nodes in one array. Boxes in double (T33).
 */


// Macros.
/** The node array's first capacity. */
#define INITIAL_NODE_CAPACITY 16U


// Static functions: boxes.
/* Half the surface area of a box (the insertion cost). */
static double GetArea(BoundingBoxDouble box)
{
    double X = box.Max.X - box.Min.X;
    double Y = box.Max.Y - box.Min.Y;
    double Z = box.Max.Z - box.Min.Z;
    return (X * Y) + (Y * Z) + (Z * X);
}


// Static functions: nodes.
static CollisionTreeNode* NodeAt(const CollisionTree* self, int32_t index)
{
    return &((CollisionTreeNode*)(void*)self->Nodes._data)[index];
}

static bool IsLeaf(const CollisionTreeNode* node)
{
    return node->Child1 == COLLISION_TREE_NULL;
}

static int32_t AllocateNode(CollisionTree* self)
{
    if (self->FreeList == COLLISION_TREE_NULL)
    {
        size_t Old = self->Nodes._count;
        size_t Grown = (Old == 0U) ? INITIAL_NODE_CAPACITY : (Old * 2U);
        GenericBuffer_EnsureTotalCapacity(&self->Nodes, Grown);
        for (size_t Index = Old; Index < Grown; Index++)
        {
            CollisionTreeNode Free = { .Parent = (Index + 1U < Grown) ? (int32_t)(Index + 1U) : COLLISION_TREE_NULL,
                .Child1 = COLLISION_TREE_NULL, .Child2 = COLLISION_TREE_NULL, .Height = -1, .UserValue = 0U };
            GenericBuffer_AddLast(&self->Nodes, &Free);
        }
        self->FreeList = (int32_t)Old;
    }
    int32_t Index = self->FreeList;
    CollisionTreeNode* Node = NodeAt(self, Index);
    self->FreeList = Node->Parent;
    Node->Parent = COLLISION_TREE_NULL;
    Node->Child1 = COLLISION_TREE_NULL;
    Node->Child2 = COLLISION_TREE_NULL;
    Node->Height = 0;
    Node->UserValue = 0U;
    return Index;
}

static void FreeNode(CollisionTree* self, int32_t index)
{
    CollisionTreeNode* Node = NodeAt(self, index);
    Node->Parent = self->FreeList;
    Node->Height = -1;
    self->FreeList = index;
}

static void Refit(CollisionTree* self, int32_t index)
{
    CollisionTreeNode* Node = NodeAt(self, index);
    const CollisionTreeNode* Child1 = NodeAt(self, Node->Child1);
    const CollisionTreeNode* Child2 = NodeAt(self, Node->Child2);
    Node->Box = BoundingBoxDouble_Union(Child1->Box, Child2->Box);
    Node->Height = 1 + ((Child1->Height > Child2->Height) ? Child1->Height : Child2->Height);
}

/* Lifts @p child (A's first child when @p isChildFirst, else its second) above A, Box2D's rotation: A takes the child's
 * shorter subtree in the child's place, the child keeps its taller one. Returns the child, now at A's place. */
static int32_t Rotate(CollisionTree* self, int32_t a, int32_t child, bool isChildFirst)
{
    CollisionTreeNode* A = NodeAt(self, a);
    CollisionTreeNode* C = NodeAt(self, child);
    int32_t F = C->Child1;
    int32_t G = C->Child2;
    CollisionTreeNode* NodeF = NodeAt(self, F);
    CollisionTreeNode* NodeG = NodeAt(self, G);

    // Swap A and C.
    C->Child1 = a;
    C->Parent = A->Parent;
    A->Parent = child;
    if (C->Parent != COLLISION_TREE_NULL)
    {
        CollisionTreeNode* Parent = NodeAt(self, C->Parent);
        if (Parent->Child1 == a)
        {
            Parent->Child1 = child;
        }
        else
        {
            Parent->Child2 = child;
        }
    }
    else
    {
        self->Root = child;
    }

    // Keep the taller of F and G above; the other goes under A where C was.
    int32_t Kept = (NodeF->Height > NodeG->Height) ? F : G;
    int32_t Moved = (NodeF->Height > NodeG->Height) ? G : F;
    C->Child2 = Kept;
    if (isChildFirst)
    {
        A->Child1 = Moved;
    }
    else
    {
        A->Child2 = Moved;
    }
    NodeAt(self, Moved)->Parent = a;
    Refit(self, a);
    Refit(self, child);
    return child;
}

/* Performs a left or right rotation if node @p a is out of balance: its taller child is lifted above it. Returns the
 * node now at its place. */
static int32_t Balance(CollisionTree* self, int32_t a)
{
    CollisionTreeNode* A = NodeAt(self, a);
    if (IsLeaf(A) || (A->Height < 2))
    {
        return a;
    }
    int32_t B = A->Child1;
    int32_t C = A->Child2;
    int32_t BalanceFactor = NodeAt(self, C)->Height - NodeAt(self, B)->Height;
    if (BalanceFactor > 1)
    {
        return Rotate(self, a, C, false);
    }
    if (BalanceFactor < -1)
    {
        return Rotate(self, a, B, true);
    }
    return a;
}

/* Refits and balances every node from @p index to the root. */
static void FixUpwards(CollisionTree* self, int32_t index)
{
    while (index != COLLISION_TREE_NULL)
    {
        index = Balance(self, index);
        Refit(self, index);
        index = NodeAt(self, index)->Parent;
    }
}

static void InsertLeaf(CollisionTree* self, int32_t leaf)
{
    if (self->Root == COLLISION_TREE_NULL)
    {
        self->Root = leaf;
        NodeAt(self, leaf)->Parent = COLLISION_TREE_NULL;
        return;
    }
    BoundingBoxDouble LeafBox = NodeAt(self, leaf)->Box;

    // The sibling that grows the tree's surface the least.
    int32_t Index = self->Root;
    while (!IsLeaf(NodeAt(self, Index)))
    {
        const CollisionTreeNode* Node = NodeAt(self, Index);
        double Area = GetArea(Node->Box);
        double CombinedArea = GetArea(BoundingBoxDouble_Union(Node->Box, LeafBox));
        double Cost = 2.0 * CombinedArea;
        double InheritanceCost = 2.0 * (CombinedArea - Area);
        double ChildCosts[2];
        int32_t Children[2] = { Node->Child1, Node->Child2 };
        for (size_t Child = 0U; Child < 2U; Child++)
        {
            const CollisionTreeNode* ChildNode = NodeAt(self, Children[Child]);
            double Grown = GetArea(BoundingBoxDouble_Union(ChildNode->Box, LeafBox));
            ChildCosts[Child] = IsLeaf(ChildNode) ? (Grown + InheritanceCost)
                : ((Grown - GetArea(ChildNode->Box)) + InheritanceCost);
        }
        if ((Cost < ChildCosts[0]) && (Cost < ChildCosts[1]))
        {
            break;
        }
        Index = (ChildCosts[0] < ChildCosts[1]) ? Children[0] : Children[1];
    }

    // A new parent for the sibling and the leaf.
    int32_t Sibling = Index;
    int32_t OldParent = NodeAt(self, Sibling)->Parent;
    int32_t NewParent = AllocateNode(self);
    CollisionTreeNode* Parent = NodeAt(self, NewParent);
    const CollisionTreeNode* SiblingNode = NodeAt(self, Sibling);
    Parent->Parent = OldParent;
    Parent->Box = BoundingBoxDouble_Union(LeafBox, SiblingNode->Box);
    Parent->Height = SiblingNode->Height + 1;
    Parent->Child1 = Sibling;
    Parent->Child2 = leaf;
    if (OldParent != COLLISION_TREE_NULL)
    {
        CollisionTreeNode* Old = NodeAt(self, OldParent);
        if (Old->Child1 == Sibling)
        {
            Old->Child1 = NewParent;
        }
        else
        {
            Old->Child2 = NewParent;
        }
    }
    else
    {
        self->Root = NewParent;
    }
    NodeAt(self, Sibling)->Parent = NewParent;
    NodeAt(self, leaf)->Parent = NewParent;
    FixUpwards(self, NodeAt(self, leaf)->Parent);
}

static void RemoveLeaf(CollisionTree* self, int32_t leaf)
{
    if (leaf == self->Root)
    {
        self->Root = COLLISION_TREE_NULL;
        return;
    }
    int32_t Parent = NodeAt(self, leaf)->Parent;
    const CollisionTreeNode* ParentNode = NodeAt(self, Parent);
    int32_t GrandParent = ParentNode->Parent;
    int32_t Sibling = (ParentNode->Child1 == leaf) ? ParentNode->Child2 : ParentNode->Child1;
    if (GrandParent != COLLISION_TREE_NULL)
    {
        CollisionTreeNode* Grand = NodeAt(self, GrandParent);
        if (Grand->Child1 == Parent)
        {
            Grand->Child1 = Sibling;
        }
        else
        {
            Grand->Child2 = Sibling;
        }
        NodeAt(self, Sibling)->Parent = GrandParent;
        FreeNode(self, Parent);
        FixUpwards(self, GrandParent);
        return;
    }
    self->Root = Sibling;
    NodeAt(self, Sibling)->Parent = COLLISION_TREE_NULL;
    FreeNode(self, Parent);
}


// Public functions.
void CollisionTree_Construct(CollisionTree* self, float margin)
{
    GenericBuffer_AllocateVariable(&self->Nodes, INITIAL_NODE_CAPACITY, sizeof(CollisionTreeNode));
    self->Root = COLLISION_TREE_NULL;
    self->FreeList = COLLISION_TREE_NULL;
    self->ProxyCount = 0U;
    self->Margin = margin;
}

void CollisionTree_Deconstruct(CollisionTree* self)
{
    Memory_Free(self->Nodes._data);
    self->Nodes._data = NULL;
    self->Root = COLLISION_TREE_NULL;
    self->FreeList = COLLISION_TREE_NULL;
    self->ProxyCount = 0U;
}

void CollisionTree_Clear(CollisionTree* self)
{
    GenericBuffer_Clear(&self->Nodes);
    self->Root = COLLISION_TREE_NULL;
    self->FreeList = COLLISION_TREE_NULL;
    self->ProxyCount = 0U;
}

int32_t CollisionTree_Insert(CollisionTree* self, BoundingBoxDouble box, uint64_t userValue)
{
    int32_t Proxy = AllocateNode(self);
    CollisionTreeNode* Node = NodeAt(self, Proxy);
    Node->Box = BoundingBoxDouble_Expand(box, (double)self->Margin);
    Node->UserValue = userValue;
    Node->Height = 0;
    InsertLeaf(self, Proxy);
    self->ProxyCount++;
    return Proxy;
}

void CollisionTree_Remove(CollisionTree* self, int32_t proxy)
{
    RemoveLeaf(self, proxy);
    FreeNode(self, proxy);
    self->ProxyCount--;
}

bool CollisionTree_Move(CollisionTree* self, int32_t proxy, BoundingBoxDouble box)
{
    CollisionTreeNode* Node = NodeAt(self, proxy);
    if (BoundingBoxDouble_ContainsBox(Node->Box, box))
    {
        return false;
    }
    RemoveLeaf(self, proxy);
    Node = NodeAt(self, proxy);
    Node->Box = BoundingBoxDouble_Expand(box, (double)self->Margin);
    InsertLeaf(self, proxy);
    return true;
}

void CollisionTree_Refit(CollisionTree* self, int32_t proxy, BoundingBoxDouble box)
{
    RemoveLeaf(self, proxy);
    CollisionTreeNode* Node = NodeAt(self, proxy);
    Node->Box = BoundingBoxDouble_Expand(box, (double)self->Margin);
    InsertLeaf(self, proxy);
}

uint64_t CollisionTree_GetUserValue(const CollisionTree* self, int32_t proxy)
{
    return NodeAt(self, proxy)->UserValue;
}

BoundingBoxDouble CollisionTree_GetBounds(const CollisionTree* self, int32_t proxy)
{
    return NodeAt(self, proxy)->Box;
}

uint32_t CollisionTree_GetProxyCount(const CollisionTree* self)
{
    return self->ProxyCount;
}

int32_t CollisionTree_GetHeight(const CollisionTree* self)
{
    return (self->Root == COLLISION_TREE_NULL) ? 0 : NodeAt(self, self->Root)->Height;
}

void CollisionTree_QueryBox(const CollisionTree* self, BoundingBoxDouble box, CollisionTreeVisitor visit,
    void* context)
{
    int32_t Stack[COLLISION_TREE_STACK_CAPACITY];
    size_t Count = 0U;
    if (self->Root == COLLISION_TREE_NULL)
    {
        return;
    }
    Stack[Count++] = self->Root;
    while (Count > 0U)
    {
        int32_t Index = Stack[--Count];
        const CollisionTreeNode* Node = NodeAt(self, Index);
        if (!BoundingBoxDouble_Overlaps(Node->Box, box))
        {
            continue;
        }
        if (IsLeaf(Node))
        {
            if (!visit(context, Index, Node->UserValue))
            {
                return;
            }
            continue;
        }
        if ((Count + 2U) <= COLLISION_TREE_STACK_CAPACITY)
        {
            Stack[Count++] = Node->Child1;
            Stack[Count++] = Node->Child2;
        }
    }
}

void CollisionTree_QueryRay(const CollisionTree* self, Vector3Double origin, Vector3 direction, float maxDistance,
    CollisionTreeRayVisitor visit, void* context)
{
    if (self->Root == COLLISION_TREE_NULL)
    {
        return;
    }
    const double Direction[3] = { direction.x, direction.y, direction.z };
    const double Origin[3] = { origin.X, origin.Y, origin.Z };
    double Inverse[3];
    for (size_t Axis = 0U; Axis < 3U; Axis++)
    {
        Inverse[Axis] = (fabs(Direction[Axis]) > 1e-12) ? (1.0 / Direction[Axis]) : copysign(DBL_MAX, Direction[Axis]);
    }
    int32_t Stack[COLLISION_TREE_STACK_CAPACITY];
    size_t Count = 0U;
    Stack[Count++] = self->Root;
    double Length = (double)maxDistance;
    while (Count > 0U)
    {
        int32_t Index = Stack[--Count];
        const CollisionTreeNode* Node = NodeAt(self, Index);
        const double Mins[3] = { Node->Box.Min.X, Node->Box.Min.Y, Node->Box.Min.Z };
        const double Maxes[3] = { Node->Box.Max.X, Node->Box.Max.Y, Node->Box.Max.Z };
        double Near = -DBL_MAX;
        double Far = DBL_MAX;
        for (size_t Axis = 0U; Axis < 3U; Axis++)
        {
            double T1 = (Mins[Axis] - Origin[Axis]) * Inverse[Axis];
            double T2 = (Maxes[Axis] - Origin[Axis]) * Inverse[Axis];
            Near = fmax(Near, fmin(T1, T2));
            Far = fmin(Far, fmax(T1, T2));
        }
        if ((Far < fmax(Near, 0.0)) || (Near > Length))
        {
            continue;
        }
        if (IsLeaf(Node))
        {
            float NewLength = visit(context, Index, Node->UserValue, (float)Length);
            if (NewLength <= 0.0f)
            {
                return;
            }
            Length = fmin(Length, (double)NewLength);
            continue;
        }
        if ((Count + 2U) <= COLLISION_TREE_STACK_CAPACITY)
        {
            Stack[Count++] = Node->Child1;
            Stack[Count++] = Node->Child2;
        }
    }
}
