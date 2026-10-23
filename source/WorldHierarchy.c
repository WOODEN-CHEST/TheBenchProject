// The world's hierarchy: parents and children (World_SetParent and the links it keeps), the cached world matrices (a
// change marks the subtree dirty, World_UpdateTransforms recomputes the dirty ones; a read of a dirty one computes it
// without storing it), the effective flags, and the subtree walks removal uses. The WorldObject_ functions about the
// hierarchy (WorldObject.h) live here too.
//
// The links are kept so that no hierarchy has a cycle or more than WORLD_HIERARCHY_MAX_DEPTH levels: World_SetParent
// and parents brought by reads both check it before linking. Upward walks are bounded by that depth on top; subtree
// walks follow the child and sibling links without recursion.

#include <inttypes.h>
#include <stdio.h>
#include "World.h"
#include "WorldInternal.h"
#include "WorldTransform.h"
#include "Logger.h"
#include "wr/WRMemory.h"


// Macros.
/** Room for the text of a refused parent's reason in the log line. */
#define REFUSAL_MESSAGE_CAPACITY 256


// Static functions: links.
/* Appends @p child (a root) to @p parent's children. */
static void LinkAsLastChild(WorldObject* child, WorldObject* parent)
{
    child->_parent = parent;
    child->_previousSibling = parent->_lastChild;
    child->_nextSibling = NULL;
    if (parent->_lastChild != NULL)
    {
        parent->_lastChild->_nextSibling = child;
    }
    else
    {
        parent->_firstChild = child;
    }
    parent->_lastChild = child;
    parent->_childCount++;
}

/* Places @p child under @p parent (NULL: makes it a root) and keeps its parent id with the link. */
static void MoveUnder(WorldObject* child, WorldObject* parent)
{
    WorldInternal_DetachFromParent(child);
    if (parent != NULL)
    {
        LinkAsLastChild(child, parent);
    }
    child->_parentId = (parent != NULL) ? parent->_id : 0U;
}

/* The object after @p current in a walk of @p root's subtree, parents before their children (in order), or NULL at the
 * subtree's end. */
static WorldObject* GetNextInSubtree(const WorldObject* current, const WorldObject* root)
{
    if (current->_firstChild != NULL)
    {
        return current->_firstChild;
    }
    while ((current != root) && (current->_nextSibling == NULL))
    {
        current = current->_parent;
    }
    return (current == root) ? NULL : current->_nextSibling;
}

/* The deepest first child below @p object (itself without children): where a children-first walk starts. */
static WorldObject* GetDeepestFirstChild(WorldObject* object)
{
    while (object->_firstChild != NULL)
    {
        object = object->_firstChild;
    }
    return object;
}

/* The signs of the scales along @p object's chain, axis by axis: its own scale's signs times every ancestor's. They are
 * its world scale's signs as long as no turn carries one axis onto another, and the decomposition's hint: a child keeps
 * the signs its scales give it (a billboard under a mirrored parent flips as one with that scale of its own would). */
static Vector3 GetChainScaleSigns(const WorldObject* object)
{
    Vector3 Signs = { 1.0f, 1.0f, 1.0f };
    size_t Level = 0U;
    for (const WorldObject* Current = object; (Current != NULL) && (Level <= WORLD_HIERARCHY_MAX_DEPTH);
        Current = Current->_parent)
    {
        Signs.x = (Current->_scale.x < 0.0f) ? -Signs.x : Signs.x;
        Signs.y = (Current->_scale.y < 0.0f) ? -Signs.y : Signs.y;
        Signs.z = (Current->_scale.z < 0.0f) ? -Signs.z : Signs.z;
        Level++;
    }
    return Signs;
}


// Static functions: the rules.
/* The object's level: 1 for a root, its parent's plus one below it. */
static size_t GetDepth(const WorldObject* object)
{
    size_t Depth = 0U;
    for (; (object != NULL) && (Depth <= WORLD_HIERARCHY_MAX_DEPTH); object = object->_parent)
    {
        Depth++;
    }
    return Depth;
}

/* How many levels @p root's subtree has: 1 without children. */
static size_t GetHeight(const WorldObject* root)
{
    size_t Level = 1U;
    size_t Height = 1U;
    const WorldObject* Current = root;
    while (Current != NULL)
    {
        Height = (Level > Height) ? Level : Height;
        if (Current->_firstChild != NULL)
        {
            Current = Current->_firstChild;
            Level++;
            continue;
        }
        while ((Current != root) && (Current->_nextSibling == NULL))
        {
            Current = Current->_parent;
            Level--;
        }
        Current = (Current == root) ? NULL : Current->_nextSibling;
    }
    return Height;
}

/* Whether @p object is @p ancestor or lies in its subtree. */
static bool IsInSubtree(const WorldObject* object, const WorldObject* ancestor)
{
    size_t Level = 0U;
    for (; (object != NULL) && (Level <= WORLD_HIERARCHY_MAX_DEPTH); object = object->_parent)
    {
        if (object == ancestor)
        {
            return true;
        }
        Level++;
    }
    return false;
}

/* Finds the parent @p parentId names for @p child and checks the hierarchy may take it: the parent exists, is not the
 * child or one of its descendants, and the child's subtree under it stays within WORLD_HIERARCHY_MAX_DEPTH levels.
 * @p parentId 0 (a root) always passes, with *outParent NULL. Messages start with @p prefix (e.g. the function's name
 * and a colon, or nothing). */
static Error FindNewParent(World* world, const WorldObject* child, uint64_t parentId, const char* prefix,
    WorldObject** outParent)
{
    *outParent = NULL;
    if (parentId == 0U)
    {
        return Error_CreateSuccess();
    }
    WorldObject* Parent = WorldInternal_FindObject(world, parentId);
    if (Parent == NULL)
    {
        return Error_Construct4(ErrorCode_InvalidOperation, "%sno object has the id %" PRIu64 " to be the parent.",
            prefix, parentId);
    }
    if (Parent == child)
    {
        return Error_Construct4(ErrorCode_InvalidOperation, "%sobject %" PRIu64 " cannot be its own parent.", prefix,
            parentId);
    }
    if (IsInSubtree(Parent, child))
    {
        return Error_Construct4(ErrorCode_InvalidOperation,
            "%sobject %" PRIu64 " lies inside object %" PRIu64 "'s subtree, so it cannot be its parent (a cycle).",
            prefix, parentId, child->_id);
    }
    if ((GetDepth(Parent) + GetHeight(child)) > WORLD_HIERARCHY_MAX_DEPTH)
    {
        return Error_Construct4(ErrorCode_InvalidOperation,
            "%sunder object %" PRIu64 " the hierarchy would have more than %zu levels.", prefix, parentId,
            WORLD_HIERARCHY_MAX_DEPTH);
    }
    *outParent = Parent;
    return Error_CreateSuccess();
}

/* Logs the first parent a read or a copy brought that the world refused. */
static void LogRefusedParentOnce(World* world, const WorldObject* object, uint64_t parentId, const Error* reason)
{
    if (world->_hasLoggedRefusedParent || (world->_logger == NULL))
    {
        return;
    }
    world->_hasLoggedRefusedParent = true;
    char Reason[REFUSAL_MESSAGE_CAPACITY];
    snprintf(Reason, sizeof(Reason), "%s", (reason->Message != NULL) ? (const char*)reason->Message : "refused");
    Error LogResult = Logger_LogWarningFormatted(world->_logger,
        u8"World: a read or a copy gave object %" PRIu64 " the parent %" PRIu64 ", which was refused: %s Object %" PRIu64
        u8" keeps its parent (%" PRIu64 "). Not logged again for this world.",
        object->_id, parentId, Reason, object->_id, object->_parentId);
    Error_Deconstruct(&LogResult);
}


// Static functions: world matrices (in double: positions are, T33).
static Matrix4Double GetLocalMatrix(const WorldObject* object)
{
    return WorldTransform_Compose(object->_position, object->_rotation, object->_scale);
}

/* The object's world matrix: its cache when up to date, otherwise its local matrix times its parent's world matrix,
 * computed the same way and stored nowhere. @p level bounds the walk up. */
static Matrix4Double ComputeWorldMatrix(const WorldObject* object, size_t level)
{
    if (!object->_isWorldMatrixDirty)
    {
        return object->_worldMatrix;
    }
    Matrix4Double Local = GetLocalMatrix(object);
    if ((object->_parent == NULL) || (level >= WORLD_HIERARCHY_MAX_DEPTH))
    {
        return Local;
    }
    Matrix4Double ParentWorld = ComputeWorldMatrix(object->_parent, level + 1U);
    return Matrix4Double_Multiply(&Local, &ParentWorld);
}

/* Computes and stores a dirty object's world matrix, its dirty ancestors' first. @p level bounds the walk up. */
static void RefreshWorldMatrix(World* world, WorldObject* object, size_t level)
{
    WorldObject* Parent = object->_parent;
    if ((Parent != NULL) && Parent->_isWorldMatrixDirty && (level < WORLD_HIERARCHY_MAX_DEPTH))
    {
        RefreshWorldMatrix(world, Parent, level + 1U);
    }
    Matrix4Double Local = GetLocalMatrix(object);
    object->_worldMatrix = (Parent != NULL) ? Matrix4Double_Multiply(&Local, &Parent->_worldMatrix) : Local;
    object->_isWorldMatrixDirty = false;
    world->_dirtyTransformCount--;
    world->_worldMatrixRecomputeCount++;
}

/* The object with @p id for the id-based getters, or an error naming @p caller. */
static Error FindForGetter(World* world, uint64_t id, const void* output, const char* caller, WorldObject** outObject)
{
    *outObject = NULL;
    if ((world == NULL) || (output == NULL) || (id == 0U))
    {
        return Error_Construct4(ErrorCode_IllegalArgument, "%s: the world and the output must not be NULL, nor the id 0.",
            caller);
    }
    *outObject = WorldInternal_FindObject(world, id);
    if (*outObject == NULL)
    {
        return Error_Construct4(ErrorCode_InvalidOperation, "%s: no object has the id %" PRIu64 ".", caller, id);
    }
    return Error_CreateSuccess();
}


// Internal functions (declared in WorldInternal.h).
void WorldInternal_MarkTransformChanged(World* world, WorldObject* object)
{
    for (WorldObject* Current = object; Current != NULL; Current = GetNextInSubtree(Current, object))
    {
        Current->_transformRevision++;
        if (!Current->_isWorldMatrixDirty)
        {
            Current->_isWorldMatrixDirty = true;
            world->_dirtyTransformCount++;
        }
    }
}

void WorldInternal_SyncObjectParent(World* world, WorldObject* object)
{
    uint64_t LinkedId = (object->_parent != NULL) ? object->_parent->_id : 0U;
    uint64_t RequestedId = object->_parentId;
    if (RequestedId == LinkedId)
    {
        return;
    }

    // Until the new parent is accepted, the id is the one the links hold.
    object->_parentId = LinkedId;
    WorldObject* Parent = NULL;
    Error Result = FindNewParent(world, object, RequestedId, "", &Parent);
    if (Result.Code != ErrorCode_Success)
    {
        world->_refusedParentCount++;
        LogRefusedParentOnce(world, object, RequestedId, &Result);
        Error_Deconstruct(&Result);
        return;
    }
    MoveUnder(object, Parent);
}

WorldObject* WorldInternal_GetFirstChildrenFirst(WorldObject* root)
{
    return GetDeepestFirstChild(root);
}

WorldObject* WorldInternal_GetNextChildrenFirst(WorldObject* current, const WorldObject* root)
{
    if (current == root)
    {
        return NULL;
    }
    return (current->_nextSibling != NULL) ? GetDeepestFirstChild(current->_nextSibling) : current->_parent;
}

bool WorldInternal_CollectSubtree(WorldObject* object, GenericBuffer* outObjects)
{
    for (WorldObject* Current = WorldInternal_GetFirstChildrenFirst(object); Current != NULL;
        Current = WorldInternal_GetNextChildrenFirst(Current, object))
    {
        if (!GenericBuffer_AddLast(outObjects, &Current))
        {
            return false;
        }
    }
    return true;
}

void WorldInternal_DetachFromParent(WorldObject* object)
{
    WorldObject* Parent = object->_parent;
    if (Parent == NULL)
    {
        return;
    }
    if (object->_previousSibling != NULL)
    {
        object->_previousSibling->_nextSibling = object->_nextSibling;
    }
    else
    {
        Parent->_firstChild = object->_nextSibling;
    }
    if (object->_nextSibling != NULL)
    {
        object->_nextSibling->_previousSibling = object->_previousSibling;
    }
    else
    {
        Parent->_lastChild = object->_previousSibling;
    }
    Parent->_childCount--;
    object->_parent = NULL;
    object->_previousSibling = NULL;
    object->_nextSibling = NULL;
    object->_parentId = 0U;
}


// Public functions: objects (declared in WorldObject.h).
Matrix4Double WorldObject_GetWorldMatrix(const WorldObject* self)
{
    return ComputeWorldMatrix(self, 0U);
}

Vector3Double WorldObject_GetWorldPosition(const WorldObject* self)
{
    if (self->_parent == NULL)
    {
        return self->_position;
    }
    Matrix4Double WorldMatrix = ComputeWorldMatrix(self, 0U);
    return Matrix4Double_GetTranslation(&WorldMatrix);
}

void WorldObject_GetWorldTransform(const WorldObject* self, Vector3Double* outPosition, Vector3* outRotation,
    Vector3* outScale)
{
    if (self->_parent == NULL)
    {
        *outPosition = self->_position;
        *outRotation = self->_rotation;
        *outScale = self->_scale;
        return;
    }
    // Its result is false only for a world matrix whose scales overflowed the float range, whose values then say so
    // themselves.
    Matrix4Double WorldMatrix = ComputeWorldMatrix(self, 0U);
    WorldTransform_Decompose(&WorldMatrix, GetChainScaleSigns(self), outPosition, outRotation, outScale);
}

bool WorldObject_IsEffectivelyEnabled(const WorldObject* self)
{
    size_t Level = 0U;
    for (const WorldObject* Current = self; (Current != NULL) && (Level <= WORLD_HIERARCHY_MAX_DEPTH);
        Current = Current->_parent)
    {
        if (!Current->_isEnabled)
        {
            return false;
        }
        Level++;
    }
    return true;
}

bool WorldObject_IsEffectivelyVisible(const WorldObject* self)
{
    size_t Level = 0U;
    for (const WorldObject* Current = self; (Current != NULL) && (Level <= WORLD_HIERARCHY_MAX_DEPTH);
        Current = Current->_parent)
    {
        if (!Current->_isVisible)
        {
            return false;
        }
        Level++;
    }
    return true;
}


// Public functions: the world (declared in World.h).
Error World_SetParent(World* self, uint64_t childId, uint64_t parentId, bool keepWorldTransform)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "World_SetParent: self must not be NULL.");
    }
    if (childId == 0U)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "World_SetParent: childId must not be 0.");
    }
    WorldObject* Child = WorldInternal_FindObject(self, childId);
    if (Child == NULL)
    {
        return Error_Construct4(ErrorCode_InvalidOperation, "World_SetParent: no object has the id %" PRIu64 ".",
            childId);
    }
    if (parentId == Child->_parentId)
    {
        return Error_CreateSuccess();
    }
    WorldObject* Parent = NULL;
    Error CheckResult = FindNewParent(self, Child, parentId, "World_SetParent: ", &Parent);
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }

    // The placement to keep, worked out before anything changes. Its scale keeps the signs it has in the world, less
    // the new parent's: then it also looks the same where the signs matter on their own (a billboard's flips).
    Vector3Double Position = Child->_position;
    Vector3 Rotation = Child->_rotation;
    Vector3 Scale = Child->_scale;
    if (keepWorldTransform)
    {
        Matrix4Double Local = ComputeWorldMatrix(Child, 0U);
        Vector3 Signs = GetChainScaleSigns(Child);
        if (Parent != NULL)
        {
            Matrix4Double ChildWorld = Local;
            Matrix4Double ParentWorld = ComputeWorldMatrix(Parent, 0U);
            if (!WorldTransform_TryGetLocalMatrix(&ChildWorld, &ParentWorld, &Local))
            {
                return Error_Construct4(ErrorCode_InvalidOperation,
                    "World_SetParent: object %" PRIu64 "'s world matrix cannot be inverted (a zero scale on an axis), "
                    "so object %" PRIu64 " cannot keep its place under it.", parentId, childId);
            }
            Vector3 ParentSigns = GetChainScaleSigns(Parent);
            Signs = (Vector3){ Signs.x * ParentSigns.x, Signs.y * ParentSigns.y, Signs.z * ParentSigns.z };
        }
        WorldTransform_Decompose(&Local, Signs, &Position, &Rotation, &Scale);
        Error TransformResult = WorldObject_CheckTransform(Position, Rotation, Scale);
        if (TransformResult.Code != ErrorCode_Success)
        {
            return TransformResult;
        }
    }

    MoveUnder(Child, Parent);
    Child->_position = Position;
    Child->_rotation = Rotation;
    Child->_scale = Scale;
    Child->_revision++;
    WorldInternal_MarkTransformChanged(self, Child);
    return Error_CreateSuccess();
}

Error World_GetParent(World* self, uint64_t id, uint64_t* outParentId)
{
    if (outParentId != NULL)
    {
        *outParentId = 0U;
    }
    WorldObject* Object = NULL;
    Error Result = FindForGetter(self, id, outParentId, "World_GetParent", &Object);
    if (Result.Code == ErrorCode_Success)
    {
        *outParentId = Object->_parentId;
    }
    return Result;
}

Error World_GetChildren(World* self, uint64_t id, GenericBuffer* outIds)
{
    if ((outIds != NULL) && (outIds->_elementSize != sizeof(uint64_t)))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "World_GetChildren: outIds must hold uint64_t.");
    }
    WorldObject* Object = NULL;
    Error Result = FindForGetter(self, id, outIds, "World_GetChildren", &Object);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    for (WorldObject* Child = Object->_firstChild; Child != NULL; Child = Child->_nextSibling)
    {
        uint64_t ChildId = Child->_id;
        if (!GenericBuffer_AddLast(outIds, &ChildId))
        {
            return Error_Construct2(ErrorCode_BufferTooSmall, "World_GetChildren: outIds could not grow.");
        }
    }
    return Error_CreateSuccess();
}

Error World_GetWorldMatrix(World* self, uint64_t id, Matrix4Double* outMatrix)
{
    WorldObject* Object = NULL;
    Error Result = FindForGetter(self, id, outMatrix, "World_GetWorldMatrix", &Object);
    if (Result.Code == ErrorCode_Success)
    {
        *outMatrix = ComputeWorldMatrix(Object, 0U);
    }
    return Result;
}

Error World_GetWorldPosition(World* self, uint64_t id, Vector3Double* outPosition)
{
    WorldObject* Object = NULL;
    Error Result = FindForGetter(self, id, outPosition, "World_GetWorldPosition", &Object);
    if (Result.Code == ErrorCode_Success)
    {
        *outPosition = WorldObject_GetWorldPosition(Object);
    }
    return Result;
}

Error World_GetWorldRotation(World* self, uint64_t id, Vector3* outRotation)
{
    WorldObject* Object = NULL;
    Error Result = FindForGetter(self, id, outRotation, "World_GetWorldRotation", &Object);
    if (Result.Code == ErrorCode_Success)
    {
        Vector3Double Position;
        Vector3 Scale;
        WorldObject_GetWorldTransform(Object, &Position, outRotation, &Scale);
    }
    return Result;
}

bool World_IsEffectivelyEnabled(World* self, uint64_t id)
{
    WorldObject* Object = NULL;
    return World_TryGetObject(self, id, &Object) && WorldObject_IsEffectivelyEnabled(Object);
}

bool World_IsEffectivelyVisible(World* self, uint64_t id)
{
    WorldObject* Object = NULL;
    return World_TryGetObject(self, id, &Object) && WorldObject_IsEffectivelyVisible(Object);
}

void World_UpdateTransforms(World* self)
{
    if ((self == NULL) || (self->_dirtyTransformCount == 0U))
    {
        return;
    }
    WorldObject* const* Objects = (WorldObject* const*)self->_objects._data;
    for (size_t Index = 0U; (Index < self->_objects._count) && (self->_dirtyTransformCount > 0U); Index++)
    {
        if (Objects[Index]->_isWorldMatrixDirty)
        {
            RefreshWorldMatrix(self, Objects[Index], 0U);
        }
    }
}
