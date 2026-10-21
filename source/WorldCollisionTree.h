#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "raylib/raylib.h"
#include "wr/WRMemory.h"

/*
 * Private header (source/): a dynamic bounding volume tree of boxes, the collision world's broadphase
 * (tests/WorldCollisionTests.c). Box2D's b2DynamicTree (Erin Catto) in C: leaves are proxies with a user value, inner
 * nodes bound their children; a new leaf goes where the surface area grows least, and rotations keep the tree balanced
 * (AVL heights), so queries stay logarithmic whatever the boxes' sizes (a terrain mesh and a cup share one tree).
 *
 * A proxy's box is FATTENED by the tree's margin: a moving proxy is re-inserted only when its tight box leaves its fat
 * one, so small moves cost nothing. The static tree uses a margin of 0.
 *
 * Nodes live in one growable array with a free list; proxy ids are node indices, stable until removed. Walks use an
 * explicit stack (no recursion); the tree's height stays near 1.44 log2(proxies), far below the stack's size.
 */


// Macros.
/** A node index that names nothing. */
#define COLLISION_TREE_NULL ((int32_t)-1)
/** The most nodes a walk can have pending (the tree's height bounds it; 1.44 log2(2^31) is about 45). */
#define COLLISION_TREE_STACK_CAPACITY 256U


// Types.
typedef struct CollisionTreeNodeStruct
{
    Vector3 Min;
    Vector3 Max;
    uint64_t UserValue;
    int32_t Parent;   // or the next free node while on the free list
    int32_t Child1;   // COLLISION_TREE_NULL for a leaf
    int32_t Child2;
    int32_t Height;   // 0 for a leaf, -1 while free
} CollisionTreeNode;

typedef struct CollisionTreeStruct
{
    GenericBuffer Nodes;  // CollisionTreeNode
    int32_t Root;
    int32_t FreeList;
    uint32_t ProxyCount;
    float Margin;
} CollisionTree;

/* Called for each proxy a query finds; returns false to stop the query. */
typedef bool (*CollisionTreeVisitor)(void* context, int32_t proxy, uint64_t userValue);

/* Called for each proxy a ray reaches; returns the ray's new length (the nearest hit so far: the walk skips everything
 * beyond it), 0 to stop, or the length it was given to go on unchanged. */
typedef float (*CollisionTreeRayVisitor)(void* context, int32_t proxy, uint64_t userValue, float maxDistance);


// Functions.
void CollisionTree_Construct(CollisionTree* self, float margin);
void CollisionTree_Deconstruct(CollisionTree* self);
/* Removes every proxy. */
void CollisionTree_Clear(CollisionTree* self);

/* Adds a proxy for the box [min, max] (fattened by the margin); returns its id. */
int32_t CollisionTree_Insert(CollisionTree* self, Vector3 min, Vector3 max, uint64_t userValue);
void CollisionTree_Remove(CollisionTree* self, int32_t proxy);
/* Moves a proxy to the box [min, max]; re-inserts it only when that box leaves its fat box. True when it was. */
bool CollisionTree_Move(CollisionTree* self, int32_t proxy, Vector3 min, Vector3 max);

uint64_t CollisionTree_GetUserValue(const CollisionTree* self, int32_t proxy);
/* The proxy's fat box. */
void CollisionTree_GetBounds(const CollisionTree* self, int32_t proxy, Vector3* outMin, Vector3* outMax);
uint32_t CollisionTree_GetProxyCount(const CollisionTree* self);
/* The root's height (0 for one proxy or none); for tests. */
int32_t CollisionTree_GetHeight(const CollisionTree* self);

/* Visits every proxy whose fat box overlaps [min, max]. */
void CollisionTree_QueryBox(const CollisionTree* self, Vector3 min, Vector3 max, CollisionTreeVisitor visit,
    void* context);
/* Visits every proxy whose fat box the ray origin + t * direction (unit direction) reaches for t in [0, maxDistance],
 * the length shrinking as the visitor reports nearer hits. */
void CollisionTree_QueryRay(const CollisionTree* self, Vector3 origin, Vector3 direction, float maxDistance,
    CollisionTreeRayVisitor visit, void* context);
