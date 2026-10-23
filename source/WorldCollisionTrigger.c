#include <float.h>
#include "WorldCollisionInternal.h"

/*
 * Trigger pairs and their events (include/WorldCollision.h, "Trigger events"): at the end of each sync, every active
 * trigger is tested against the colliders around it; the overlapping pairs (sorted by trigger id, then other id) are
 * compared with the last sync's, and an enter is queued for each new pair, then an exit for each pair that ended. A
 * pair that already overlapped stays until the two are WORLD_COLLISION_TRIGGER_EXIT_MARGIN apart (no flicker at a
 * boundary).
 */


// Static functions.
static int ComparePairs(const WorldCollisionTriggerPair* a, const WorldCollisionTriggerPair* b)
{
    if (a->TriggerId != b->TriggerId)
    {
        return (a->TriggerId < b->TriggerId) ? -1 : 1;
    }
    if (a->OtherId != b->OtherId)
    {
        return (a->OtherId < b->OtherId) ? -1 : 1;
    }
    return 0;
}

static ComparisonResult ComparePairElements(GenericBuffer* buffer, GenericBufferElementData a,
    GenericBufferElementData b, const UserData* userData)
{
    (void)buffer;
    (void)userData;
    int Order = ComparePairs(a._element, b._element);
    return (Order < 0) ? ComparisonResult_ALessThanB : ((Order > 0) ? ComparisonResult_AGreaterThanB
        : ComparisonResult_AEqualsB);
}

static bool ContainsPair(const GenericBuffer* pairs, const WorldCollisionTriggerPair* pair)
{
    const WorldCollisionTriggerPair* Entries = (const WorldCollisionTriggerPair*)(const void*)pairs->_data;
    size_t Low = 0U;
    size_t High = pairs->_count;
    while (Low < High)
    {
        size_t Middle = Low + ((High - Low) / 2U);
        int Order = ComparePairs(&Entries[Middle], pair);
        if (Order == 0)
        {
            return true;
        }
        if (Order < 0)
        {
            Low = Middle + 1U;
        }
        else
        {
            High = Middle;
        }
    }
    return false;
}

/* The box around a trigger's valid shapes; false when one of them is an unbounded plane (it reaches everything). */
static bool GetTriggerBounds(const WorldCollisionInstance* trigger, BoundingBoxDouble* outBox)
{
    *outBox = WorldCollisionInternal_GetEmptyBox();
    for (uint32_t Index = 0U; Index < (uint32_t)trigger->Shapes._count; Index++)
    {
        const WorldCollisionShape* Shape = WorldCollisionInternal_GetShape(trigger, Index);
        if (!Shape->IsValid)
        {
            continue;
        }
        if (Shape->IsUnbounded)
        {
            return false;
        }
        *outBox = BoundingBoxDouble_Union(*outBox, Shape->Box);
    }
    return true;
}

/* The pairs of one trigger: every collider around it that qualifies and touches it. */
static void FindPairs(WorldCollision* self, const WorldCollisionInstance* trigger)
{
    BoundingBoxDouble Box;
    if (!GetTriggerBounds(trigger, &Box))
    {
        Box = (BoundingBoxDouble){ .Min = { .X = -DBL_MAX, .Y = -DBL_MAX, .Z = -DBL_MAX },
            .Max = { .X = DBL_MAX, .Y = DBL_MAX, .Z = DBL_MAX } };
    }
    WorldCollisionInternal_CollectCandidates(self, BoundingBoxDouble_Expand(Box,
        (double)WORLD_COLLISION_TRIGGER_EXIT_MARGIN));
    size_t FirstPair = self->_newTriggerPairs._count;
    for (size_t Candidate = 0U; Candidate < self->_candidates._count; Candidate++)
    {
        uint64_t Key = ((const uint64_t*)(const void*)self->_candidates._data)[Candidate];
        const WorldCollisionInstance* Other = WorldCollisionInternal_GetInstance(self,
            WorldCollisionInternal_GetKeySlot(Key));
        bool IsQualified = Other->IsUsed && Other->IsActive && (Other != trigger) && !Other->Settings.IsTrigger
            && !(trigger->IsStatic && Other->IsStatic)
            && CollisionLayer_DoCollide(trigger->Settings.Layer, trigger->Settings.CollidesWith, Other->Settings.Layer,
                Other->Settings.CollidesWith);
        if (!IsQualified)
        {
            continue;
        }
        WorldCollisionTriggerPair Pair = { .TriggerId = trigger->ObjectId, .OtherId = Other->ObjectId };
        bool IsListed = false;
        const WorldCollisionTriggerPair* Added = (const WorldCollisionTriggerPair*)(const void*)self->_newTriggerPairs._data;
        for (size_t Index = FirstPair; (Index < self->_newTriggerPairs._count) && !IsListed; Index++)
        {
            IsListed = Added[Index].OtherId == Pair.OtherId;
        }
        if (IsListed)
        {
            continue; // the other's next shape: the pair is decided
        }
        float Reach = ContainsPair(&self->_triggerPairs, &Pair) ? WORLD_COLLISION_TRIGGER_EXIT_MARGIN : 0.0f;
        if (WorldCollisionInternal_DoInstancesTouch(self, trigger, Other, Reach))
        {
            GenericBuffer_AddLast(&self->_newTriggerPairs, &Pair);
        }
    }
}

static void QueueEvent(WorldCollision* self, WorldTriggerEventKind kind, const WorldCollisionTriggerPair* pair)
{
    WorldTriggerEvent Event = { .Kind = kind, .TriggerId = pair->TriggerId, .OtherId = pair->OtherId,
        .Tick = self->_tick };
    GenericBuffer_AddLast(&self->_triggerEvents, &Event);
}


// Internal functions (WorldCollisionInternal.h).
void WorldCollisionInternal_UpdateTriggers(WorldCollision* self)
{
    GenericBuffer_Clear(&self->_newTriggerPairs);
    for (uint32_t Slot = 0U; Slot < (uint32_t)self->_instances._count; Slot++)
    {
        const WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self, Slot);
        if (Instance->IsUsed && Instance->IsActive && Instance->Settings.IsTrigger)
        {
            FindPairs(self, Instance);
        }
    }
    if (self->_newTriggerPairs._count > 1U)
    {
        WorldCollisionTriggerPair Scratch[2];
        GenericBuffer_SortAscending(&self->_newTriggerPairs, ComparePairElements, NULL, Scratch);
    }
    const WorldCollisionTriggerPair* Now = (const WorldCollisionTriggerPair*)(const void*)self->_newTriggerPairs._data;
    const WorldCollisionTriggerPair* Before = (const WorldCollisionTriggerPair*)(const void*)self->_triggerPairs._data;
    for (size_t Index = 0U; Index < self->_newTriggerPairs._count; Index++)
    {
        if (!ContainsPair(&self->_triggerPairs, &Now[Index]))
        {
            QueueEvent(self, WorldTriggerEventKind_Enter, &Now[Index]);
        }
    }
    for (size_t Index = 0U; Index < self->_triggerPairs._count; Index++)
    {
        if (!ContainsPair(&self->_newTriggerPairs, &Before[Index]))
        {
            QueueEvent(self, WorldTriggerEventKind_Exit, &Before[Index]);
        }
    }
    GenericBuffer_Clear(&self->_triggerPairs);
    GenericBuffer_AddLastRange(&self->_triggerPairs, self->_newTriggerPairs._data, self->_newTriggerPairs._count);
}
