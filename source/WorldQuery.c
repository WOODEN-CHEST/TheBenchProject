#include "WorldQuery.h"
#include "WorldInternal.h"
#include "wr/WRCompile.h"
#include "wr/WRComparator.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"


// Macros.
/** 2^63 as a double: an int64 is below it and at least its negation. */
#define QUERY_INT64_RANGE_END 9223372036854775808.0


// Static functions: checking the query.
/* An array may be NULL only when empty, and holds no key id 0. */
static bool AreKeysValid(const uint32_t* keys, size_t count)
{
    if ((keys == NULL) && (count > 0U))
    {
        return false;
    }
    for (size_t Index = 0U; Index < count; Index++)
    {
        if (keys[Index] == NAMESPACED_KEY_ID_NONE)
        {
            return false;
        }
    }
    return true;
}

static bool IsOrderingOperator(WorldDataOperator op)
{
    return (op == WorldDataOperator_Less) || (op == WorldDataOperator_LessOrEqual) || (op == WorldDataOperator_Greater)
        || (op == WorldDataOperator_GreaterOrEqual);
}

static Error CheckComparisons(const World* world, const WorldQuery* query)
{
    if ((query->Comparisons == NULL) && (query->ComparisonCount > 0U))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "World_Query: Comparisons is NULL but has a count.");
    }
    for (size_t Index = 0U; Index < query->ComparisonCount; Index++)
    {
        const WorldDataComparison* Comparison = &query->Comparisons[Index];
        DataType Type = Comparison->Value.Type;
        bool IsOrderable = (Type == DataType_Int) || (Type == DataType_Float) || (Type == DataType_DateTime);
        if ((Comparison->Key == NAMESPACED_KEY_ID_NONE) || ((int)Comparison->Operator < 0)
            || (Comparison->Operator > WorldDataOperator_GreaterOrEqual)
            || (IsOrderingOperator(Comparison->Operator) && !IsOrderable))
        {
            return Error_Construct2(ErrorCode_IllegalArgument,
                "World_Query: a comparison needs a key, a known operator, and a number or date_time to order by.");
        }
        Error ValueResult = DataValue_Validate(&Comparison->Value, world->_keys);
        if (ValueResult.Code != ErrorCode_Success)
        {
            Error Result = Error_Construct4(ErrorCode_IllegalArgument, "World_Query: a comparison's value is invalid: %s",
                (ValueResult.Message != NULL) ? (const char*)ValueResult.Message : "?");
            Error_Deconstruct(&ValueResult);
            return Result;
        }
    }
    return Error_CreateSuccess();
}

static Error CheckQuery(const World* world, const WorldQuery* query)
{
    if (!AreKeysValid(query->AllOfTags, query->AllOfTagCount) || !AreKeysValid(query->AnyOfTags, query->AnyOfTagCount)
        || !AreKeysValid(query->NoneOfTags, query->NoneOfTagCount) || !AreKeysValid(query->DataKeys, query->DataKeyCount))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "World_Query: a tag or key array is NULL with a count, or holds the key id 0.");
    }
    if ((query->ComponentTypes == NULL) && (query->ComponentTypeCount > 0U))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "World_Query: ComponentTypes is NULL but has a count.");
    }
    for (size_t Index = 0U; Index < query->ComponentTypeCount; Index++)
    {
        if (World_GetComponentTypeInfo(world, query->ComponentTypes[Index]) == NULL)
        {
            return Error_Construct2(ErrorCode_IllegalArgument, "World_Query: a component type is not one of the world's.");
        }
    }
    bool IsDistanceValid = !query->HasMaximumDistance
        || (WorldInternal_IsFloatFinite(query->MaximumDistance) && (query->MaximumDistance >= 0.0f));
    bool IsBoxValid = !query->HasBox || (Vector3Double_IsFinite(query->BoxMinimum)
        && Vector3Double_IsFinite(query->BoxMaximum) && (query->BoxMinimum.X <= query->BoxMaximum.X)
        && (query->BoxMinimum.Y <= query->BoxMaximum.Y) && (query->BoxMinimum.Z <= query->BoxMaximum.Z));
    if (!Vector3Double_IsFinite(query->Origin) || !IsDistanceValid || !IsBoxValid)
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "World_Query: the origin must be finite, the distance finite and at least 0, and the box finite and ordered.");
    }
    return CheckComparisons(world, query);
}


// Static functions: comparing values.
/* -1, 0 or 1: an int against a double, exactly (no rounding of large ints). */
static int CompareIntToDouble(int64_t integer, double value)
{
    if (value >= QUERY_INT64_RANGE_END)
    {
        return -1;
    }
    if (value < -QUERY_INT64_RANGE_END)
    {
        return 1;
    }
    double Whole = Math_TruncateDouble(value);
    int64_t WholeInteger = (int64_t)Whole;
    if (integer != WholeInteger)
    {
        return (integer < WholeInteger) ? -1 : 1;
    }
    double Fraction = value - Whole;
    return (Fraction > 0.0) ? -1 : ((Fraction < 0.0) ? 1 : 0);
}

/* -1, 0 or 1 for two numbers (ints and floats, by value). */
static int CompareNumbers(const DataValue* a, const DataValue* b)
{
    if ((a->Type == DataType_Int) && (b->Type == DataType_Int))
    {
        return (a->IntValue < b->IntValue) ? -1 : ((a->IntValue > b->IntValue) ? 1 : 0);
    }
    if ((a->Type == DataType_Float) && (b->Type == DataType_Float))
    {
        return (a->FloatValue < b->FloatValue) ? -1 : ((a->FloatValue > b->FloatValue) ? 1 : 0);
    }
    return (a->Type == DataType_Int) ? CompareIntToDouble(a->IntValue, b->FloatValue)
        : -CompareIntToDouble(b->IntValue, a->FloatValue);
}

static bool IsNumber(const DataValue* value)
{
    return (value->Type == DataType_Int) || (value->Type == DataType_Float);
}

static bool IsComparisonTrue(const DataValue* value, const WorldDataComparison* comparison)
{
    const DataValue* Operand = &comparison->Value;
    bool AreNumbers = IsNumber(value) && IsNumber(Operand);
    if ((comparison->Operator == WorldDataOperator_Equal) || (comparison->Operator == WorldDataOperator_NotEqual))
    {
        bool IsEqual = AreNumbers ? (CompareNumbers(value, Operand) == 0) : DataValue_Equals(value, Operand);
        return IsEqual == (comparison->Operator == WorldDataOperator_Equal);
    }

    int Order = 0;
    if (AreNumbers)
    {
        Order = CompareNumbers(value, Operand);
    }
    else if ((value->Type == DataType_DateTime) && (Operand->Type == DataType_DateTime))
    {
        Order = (value->DateTimeValue < Operand->DateTimeValue) ? -1
            : ((value->DateTimeValue > Operand->DateTimeValue) ? 1 : 0);
    }
    else
    {
        return false;
    }
    switch (comparison->Operator)
    {
        case WorldDataOperator_Less:
            return Order < 0;
        case WorldDataOperator_LessOrEqual:
            return Order <= 0;
        case WorldDataOperator_Greater:
            return Order > 0;
        case WorldDataOperator_GreaterOrEqual:
            return Order >= 0;
        case WorldDataOperator_Equal:
        case WorldDataOperator_NotEqual:
            break;
    }
    return false;
}


// Static functions: matching one object.
static bool MatchesTags(const WorldObject* object, const WorldQuery* query)
{
    for (size_t Index = 0U; Index < query->AllOfTagCount; Index++)
    {
        if (!WorldObject_HasTag(object, query->AllOfTags[Index]))
        {
            return false;
        }
    }
    for (size_t Index = 0U; Index < query->NoneOfTagCount; Index++)
    {
        if (WorldObject_HasTag(object, query->NoneOfTags[Index]))
        {
            return false;
        }
    }
    bool HasAny = query->AnyOfTagCount == 0U;
    for (size_t Index = 0U; (Index < query->AnyOfTagCount) && !HasAny; Index++)
    {
        HasAny = WorldObject_HasTag(object, query->AnyOfTags[Index]);
    }
    return HasAny;
}

static bool MatchesData(const WorldObject* object, const WorldQuery* query)
{
    const DataMap* Data = WorldObject_GetData(object);
    for (size_t Index = 0U; Index < query->DataKeyCount; Index++)
    {
        if (DataMap_Find(Data, query->DataKeys[Index]) == NULL)
        {
            return false;
        }
    }
    for (size_t Index = 0U; Index < query->ComparisonCount; Index++)
    {
        const DataValue* Value = DataMap_Find(Data, query->Comparisons[Index].Key);
        if ((Value == NULL) || !IsComparisonTrue(Value, &query->Comparisons[Index]))
        {
            return false;
        }
    }
    return true;
}

/* In double, as the positions are (T33): far from the world's origin a float would round both to millimetres. */
static bool MatchesPlace(const WorldObject* object, const WorldQuery* query)
{
    if (!query->HasMaximumDistance && !query->HasBox)
    {
        return true;
    }
    Vector3Double Position = WorldObject_GetWorldPosition(object);
    double MaximumDistance = (double)query->MaximumDistance;
    if (query->HasMaximumDistance
        && (Vector3Double_DistanceSquared(Position, query->Origin) > (MaximumDistance * MaximumDistance)))
    {
        return false;
    }
    return !query->HasBox || ((Position.X >= query->BoxMinimum.X) && (Position.X <= query->BoxMaximum.X)
        && (Position.Y >= query->BoxMinimum.Y) && (Position.Y <= query->BoxMaximum.Y)
        && (Position.Z >= query->BoxMinimum.Z) && (Position.Z <= query->BoxMaximum.Z));
}

static bool Matches(const WorldObject* object, const WorldQuery* query)
{
    if ((query->IsEnabledOnly && !WorldObject_IsEffectivelyEnabled(object))
        || (query->IsVisibleOnly && !WorldObject_IsEffectivelyVisible(object)))
    {
        return false;
    }
    for (size_t Index = 0U; Index < query->ComponentTypeCount; Index++)
    {
        if (WorldObject_GetComponent(object, query->ComponentTypes[Index]) == NULL)
        {
            return false;
        }
    }
    return MatchesTags(object, query) && MatchesData(object, query) && MatchesPlace(object, query);
}


// Static functions: candidates and order.
static ComparisonResult CompareIds(GenericBuffer* buffer, GenericBufferElementData a, GenericBufferElementData b,
    const UserData* userData)
{
    UNUSED(buffer);
    UNUSED(userData);
    uint64_t First = 0U;
    uint64_t Second = 0U;
    Memory_Copy(a._element, &First, sizeof(First));
    Memory_Copy(b._element, &Second, sizeof(Second));
    return COMPARE_NUMBER(First, Second);
}

/* Nearest first, ties by id (so the order never depends on the scan's). */
static ComparisonResult CompareMatches(GenericBuffer* buffer, GenericBufferElementData a, GenericBufferElementData b,
    const UserData* userData)
{
    UNUSED(buffer);
    UNUSED(userData);
    WorldQueryMatch First;
    WorldQueryMatch Second;
    Memory_Copy(a._element, &First, sizeof(First));
    Memory_Copy(b._element, &Second, sizeof(Second));
    if (First.DistanceSquared != Second.DistanceSquared)
    {
        return (First.DistanceSquared < Second.DistanceSquared) ? ComparisonResult_ALessThanB
            : ComparisonResult_AGreaterThanB;
    }
    return COMPARE_NUMBER(First.Id, Second.Id);
}

/* The candidate ids from the tag index, ascending: the smallest all-of list, or the any-of lists merged (an id that
 * carries several of them repeats). False when the query has neither (every object is a candidate). */
static bool CollectTaggedCandidates(World* world, const WorldQuery* query, const uint64_t** outIds, size_t* outCount)
{
    if (query->AllOfTagCount > 0U)
    {
        *outIds = WorldInternal_GetTagMembers(world, query->AllOfTags[0], outCount);
        for (size_t Index = 1U; (Index < query->AllOfTagCount) && (*outCount > 0U); Index++)
        {
            size_t Count = 0U;
            const uint64_t* Ids = WorldInternal_GetTagMembers(world, query->AllOfTags[Index], &Count);
            if (Count < *outCount)
            {
                *outIds = Ids;
                *outCount = Count;
            }
        }
        return true;
    }
    if (query->AnyOfTagCount == 0U)
    {
        return false;
    }

    GenericBuffer* Candidates = &world->_queryCandidates;
    GenericBuffer_Clear(Candidates);
    for (size_t Index = 0U; Index < query->AnyOfTagCount; Index++)
    {
        size_t Count = 0U;
        const uint64_t* Ids = WorldInternal_GetTagMembers(world, query->AnyOfTags[Index], &Count);
        for (size_t Member = 0U; Member < Count; Member++)
        {
            uint64_t Id = Ids[Member];
            GenericBuffer_AddLast(Candidates, &Id);
        }
    }
    uint64_t Scratch[2];
    GenericBuffer_SortAscending(Candidates, CompareIds, NULL, Scratch);
    *outIds = (const uint64_t*)Candidates->_data;
    *outCount = Candidates->_count;
    return true;
}

/* Adds a matching object to the world's match scratch; returns false once the limit is reached and nothing later can
 * come before what was kept (ids ascending, no distance sort). */
static bool KeepMatch(World* world, const WorldQuery* query, const WorldObject* object, bool areCandidatesOrdered)
{
    WorldQueryMatch Match = { .Id = WorldObject_GetId(object),
        .DistanceSquared = query->IsSortedByDistance
            ? Vector3Double_DistanceSquared(WorldObject_GetWorldPosition(object), query->Origin) : 0.0 };
    GenericBuffer_AddLast(&world->_queryMatches, &Match);
    bool IsFull = (query->Limit > 0U) && (world->_queryMatches._count >= query->Limit);
    return !(IsFull && areCandidatesOrdered && !query->IsSortedByDistance);
}


// Public functions.
Error World_Query(World* self, const WorldQuery* query, GenericBuffer* outIds)
{
    if ((self == NULL) || (query == NULL) || (outIds == NULL) || (outIds->_elementSize != sizeof(uint64_t)))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "World_Query: self, query and outIds (a buffer of uint64_t) must not be NULL.");
    }
    Error Result = CheckQuery(self, query);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    GenericBuffer_Clear(&self->_queryMatches);
    const uint64_t* Candidates = NULL;
    size_t CandidateCount = 0U;
    bool AreCandidatesOrdered = CollectTaggedCandidates(self, query, &Candidates, &CandidateCount);
    if (AreCandidatesOrdered)
    {
        for (size_t Index = 0U; Index < CandidateCount; Index++)
        {
            if ((Index > 0U) && (Candidates[Index] == Candidates[Index - 1U]))
            {
                continue; // carries more than one of the any-of tags
            }
            WorldObject* Object = WorldInternal_FindObject(self, Candidates[Index]);
            if ((Object != NULL) && Matches(Object, query) && !KeepMatch(self, query, Object, true))
            {
                break;
            }
        }
    }
    else
    {
        for (size_t Index = 0U; Index < World_GetObjectCount(self); Index++)
        {
            WorldObject* Object = NULL;
            GenericBuffer_GetAt(&self->_objects, Index, &Object);
            if (Matches(Object, query))
            {
                KeepMatch(self, query, Object, false);
            }
        }
    }

    // Order: nearest first, or ids ascending (the tag index gives them in that order already).
    WorldQueryMatch Scratch[2];
    if (query->IsSortedByDistance || !AreCandidatesOrdered)
    {
        GenericBuffer_SortAscending(&self->_queryMatches, CompareMatches, NULL, Scratch);
    }
    size_t Count = self->_queryMatches._count;
    Count = ((query->Limit > 0U) && (query->Limit < Count)) ? query->Limit : Count;
    const WorldQueryMatch* Kept = (const WorldQueryMatch*)self->_queryMatches._data;
    for (size_t Index = 0U; Index < Count; Index++)
    {
        uint64_t Id = Kept[Index].Id;
        if (!GenericBuffer_AddLast(outIds, &Id))
        {
            return Error_Construct2(ErrorCode_BufferTooSmall, "World_Query: outIds could not grow.");
        }
    }
    return Error_CreateSuccess();
}

Error World_DoesObjectMatch(World* self, const WorldQuery* query, uint64_t id, bool* outIsMatch)
{
    if ((self == NULL) || (query == NULL) || (outIsMatch == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "World_DoesObjectMatch: self, query and outIsMatch must not be NULL.");
    }
    *outIsMatch = false;
    Error Result = CheckQuery(self, query);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    WorldObject* Object = WorldInternal_FindObject(self, id);
    *outIsMatch = (Object != NULL) && Matches(Object, query);
    return Error_CreateSuccess();
}
