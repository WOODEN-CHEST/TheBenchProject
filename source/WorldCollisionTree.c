#include <float.h>
#include <math.h>
#include "WorldCollisionTree.h"

/*
 * The dynamic bounding volume tree (source/WorldCollisionTree.h), after Box2D's b2DynamicTree: surface-area-guided
 * insertion, AVL rotations on the way back up, a free list of nodes in one array.
 */


// Macros.
/** The node array's first capacity. */
#define INITIAL_NODE_CAPACITY 16U


// Static functions: boxes.
static Vector3 MinVector(Vector3 a, Vector3 b)
{
    return (Vector3){ .x = fminf(a.x, b.x), .y = fminf(a.y, b.y), .z = fminf(a.z, b.z) };
}

static Vector3 MaxVector(Vector3 a, Vector3 b)
{
    return (Vector3){ .x = fmaxf(a.x, b.x), .y = fmaxf(a.y, b.y), .z = fmaxf(a.z, b.z) };
}

/* Half the surface area of a box (the insertion cost). */
static float GetArea(Vector3 min, Vector3 max)
{
    float X = max.x - min.x;
    float Y = max.y - min.y;
    float Z = max.z - min.z;
    return (X * Y) + (Y * Z) + (Z * X);
}

static bool Overlaps(Vector3 minA, Vector3 maxA, Vector3 minB, Vector3 maxB)
{
    return (minA.x <= maxB.x) && (maxA.x >= minB.x) && (minA.y <= maxB.y) && (maxA.y >= minB.y)
        && (minA.z <= maxB.z) && (maxA.z >= minB.z);
}

static bool Contains(Vector3 outerMin, Vector3 outerMax, Vector3 innerMin, Vector3 innerMax)
{
    return (outerMin.x <= innerMin.x) && (outerMin.y <= innerMin.y) && (outerMin.z <= innerMin.z)
        && (innerMax.x <= outerMax.x) && (innerMax.y <= outerMax.y) && (innerMax.z <= outerMax.z);
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
    Node->Min = MinVector(Child1->Min, Child2->Min);
    Node->Max = MaxVector(Child1->Max, Child2->Max);
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
    Vector3 LeafMin = NodeAt(self, leaf)->Min;
    Vector3 LeafMax = NodeAt(self, leaf)->Max;

    // The sibling that grows the tree's surface the least.
    int32_t Index = self->Root;
    while (!IsLeaf(NodeAt(self, Index)))
    {
        const CollisionTreeNode* Node = NodeAt(self, Index);
        float Area = GetArea(Node->Min, Node->Max);
        float CombinedArea = GetArea(MinVector(Node->Min, LeafMin), MaxVector(Node->Max, LeafMax));
        float Cost = 2.0f * CombinedArea;
        float InheritanceCost = 2.0f * (CombinedArea - Area);
        float ChildCosts[2];
        int32_t Children[2] = { Node->Child1, Node->Child2 };
        for (size_t Child = 0U; Child < 2U; Child++)
        {
            const CollisionTreeNode* ChildNode = NodeAt(self, Children[Child]);
            float Grown = GetArea(MinVector(ChildNode->Min, LeafMin), MaxVector(ChildNode->Max, LeafMax));
            ChildCosts[Child] = IsLeaf(ChildNode) ? (Grown + InheritanceCost)
                : ((Grown - GetArea(ChildNode->Min, ChildNode->Max)) + InheritanceCost);
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
    Parent->Min = MinVector(LeafMin, SiblingNode->Min);
    Parent->Max = MaxVector(LeafMax, SiblingNode->Max);
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

int32_t CollisionTree_Insert(CollisionTree* self, Vector3 min, Vector3 max, uint64_t userValue)
{
    int32_t Proxy = AllocateNode(self);
    CollisionTreeNode* Node = NodeAt(self, Proxy);
    Vector3 Margin = { self->Margin, self->Margin, self->Margin };
    Node->Min = (Vector3){ min.x - Margin.x, min.y - Margin.y, min.z - Margin.z };
    Node->Max = (Vector3){ max.x + Margin.x, max.y + Margin.y, max.z + Margin.z };
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

bool CollisionTree_Move(CollisionTree* self, int32_t proxy, Vector3 min, Vector3 max)
{
    CollisionTreeNode* Node = NodeAt(self, proxy);
    if (Contains(Node->Min, Node->Max, min, max))
    {
        return false;
    }
    RemoveLeaf(self, proxy);
    Node = NodeAt(self, proxy);
    Node->Min = (Vector3){ min.x - self->Margin, min.y - self->Margin, min.z - self->Margin };
    Node->Max = (Vector3){ max.x + self->Margin, max.y + self->Margin, max.z + self->Margin };
    InsertLeaf(self, proxy);
    return true;
}

uint64_t CollisionTree_GetUserValue(const CollisionTree* self, int32_t proxy)
{
    return NodeAt(self, proxy)->UserValue;
}

void CollisionTree_GetBounds(const CollisionTree* self, int32_t proxy, Vector3* outMin, Vector3* outMax)
{
    *outMin = NodeAt(self, proxy)->Min;
    *outMax = NodeAt(self, proxy)->Max;
}

uint32_t CollisionTree_GetProxyCount(const CollisionTree* self)
{
    return self->ProxyCount;
}

int32_t CollisionTree_GetHeight(const CollisionTree* self)
{
    return (self->Root == COLLISION_TREE_NULL) ? 0 : NodeAt(self, self->Root)->Height;
}

void CollisionTree_QueryBox(const CollisionTree* self, Vector3 min, Vector3 max, CollisionTreeVisitor visit,
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
        if (!Overlaps(Node->Min, Node->Max, min, max))
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

void CollisionTree_QueryRay(const CollisionTree* self, Vector3 origin, Vector3 direction, float maxDistance,
    CollisionTreeRayVisitor visit, void* context)
{
    if (self->Root == COLLISION_TREE_NULL)
    {
        return;
    }
    Vector3 Inverse = { (fabsf(direction.x) > 1e-12f) ? (1.0f / direction.x) : copysignf(FLT_MAX, direction.x),
        (fabsf(direction.y) > 1e-12f) ? (1.0f / direction.y) : copysignf(FLT_MAX, direction.y),
        (fabsf(direction.z) > 1e-12f) ? (1.0f / direction.z) : copysignf(FLT_MAX, direction.z) };
    int32_t Stack[COLLISION_TREE_STACK_CAPACITY];
    size_t Count = 0U;
    Stack[Count++] = self->Root;
    float Length = maxDistance;
    while (Count > 0U)
    {
        int32_t Index = Stack[--Count];
        const CollisionTreeNode* Node = NodeAt(self, Index);
        float T1 = (Node->Min.x - origin.x) * Inverse.x;
        float T2 = (Node->Max.x - origin.x) * Inverse.x;
        float Near = fminf(T1, T2);
        float Far = fmaxf(T1, T2);
        T1 = (Node->Min.y - origin.y) * Inverse.y;
        T2 = (Node->Max.y - origin.y) * Inverse.y;
        Near = fmaxf(Near, fminf(T1, T2));
        Far = fminf(Far, fmaxf(T1, T2));
        T1 = (Node->Min.z - origin.z) * Inverse.z;
        T2 = (Node->Max.z - origin.z) * Inverse.z;
        Near = fmaxf(Near, fminf(T1, T2));
        Far = fminf(Far, fmaxf(T1, T2));
        if ((Far < fmaxf(Near, 0.0f)) || (Near > Length))
        {
            continue;
        }
        if (IsLeaf(Node))
        {
            float NewLength = visit(context, Index, Node->UserValue, Length);
            if (NewLength <= 0.0f)
            {
                return;
            }
            Length = fminf(Length, NewLength);
            continue;
        }
        if ((Count + 2U) <= COLLISION_TREE_STACK_CAPACITY)
        {
            Stack[Count++] = Node->Child1;
            Stack[Count++] = Node->Child2;
        }
    }
}
