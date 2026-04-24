#include "test/EngineTest.h"
#include "test/ListTest.h"
#include "WRError.h"
#include "WRList.h"
#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>

#define SIMPLE_MUTATION_ELEMENT int32_t
#define SPECIAL_MUTATION_ELEMENT int64_t
#define ACCESSORS_ELEMENT int32_t
#define TECHNICAL_TEST_ELEMENT int16_t
#define ALGORITHMS_ELEMENT double
#define TRANSFORMATION_ELEMENT int32_t
#define TRANSFORMATION_DEST_ELEMENT int64_t
#define FULL_MANIPULATION_ELEMENT uint64_t
#define POP_OPERATION_ELEMENT int32_t


// Types.
typedef bool (*ListOperator)(TestErrorMessage* errorMsg, ErrorMessagePool* errorPool, WRList* list);

typedef struct ListOperationStruct
{
    ListOperator _operator;
    size_t _elementSize;
} ListOperation;


/* I fucking give up on not using magic numbers and code duplication, too much work to avoid it here. */

// Static functions.

/* Helpers. */
static bool OperateOnNewList(TestErrorMessage* errorMsg,
    ErrorMessagePool* errorPool,
    ListOperator operator,
    size_t elementSize)
{
    WRList List;
    Error ErrorResult = WRList_Construct1(&List, elementSize, errorPool);
    if (ErrorResult.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"Error constructing list for operation test: %s",
            ErrorMessageOrDefault(ErrorResult.Message));
            ErrorMessagePool_Clear(errorPool);
        return false;
    }
    bool Result = (*operator)(errorMsg, errorPool, &List);
    WRList_Deconstruct1(&List);
    return Result;
}

static bool ExecuteOperations(ListOperation* operations,
    size_t operationArraySize,
    TestErrorMessage* errorMsg,
    ErrorMessagePool* errorPool)
{
    size_t OperationCount = operationArraySize / sizeof(operations[0]);

    for (size_t i = 0; i < OperationCount; i++)
    {
        ListOperation* TargetOperation = &operations[i];
        if (!OperateOnNewList(errorMsg, errorPool, TargetOperation->_operator, TargetOperation->_elementSize))
        {
            return false;
        }
    }
    return true;
}

static bool VerifyListProperties(TestErrorMessage* errorMsg,
    Error listCreationResult,
    WRList* list,
    size_t expectedCapacity,
    size_t expectedElementCount,
    bool shouldBeAllocated,
    size_t elementSize,
    bool shouldBeWrapperList,
    bool shouldBeFixedCapacityList)
{
    if (listCreationResult.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"Error constructing list: %s",
            ErrorMessageOrDefault(listCreationResult.Message));
        return false;
    }
    bool IsAllocated = WRList_GetDataArray(list) != NULL;
    if (IsAllocated != shouldBeAllocated)
    {
        Test_FormatErrorMessage(errorMsg, u8"List's allocations status is %d, expected %d.", IsAllocated, shouldBeAllocated);
        return false;
    }
    if (WRList_GetCount(list) != expectedElementCount)
    {
        Test_FormatErrorMessage(errorMsg, u8"Element count in list is %zu, expected %zu.", (WRList_GetCount(list), expectedElementCount));
        return false;
    }
    if (WRList_GetElementSize(list) != elementSize)
    {
        Test_FormatErrorMessage(errorMsg, u8"Element size in list is %zu, expected %zu.", WRList_GetElementSize(list), elementSize);
        return false;
    }
    bool IsFixedCapacity = WRList_IsFixedCapacity(list);
    if (IsFixedCapacity != shouldBeFixedCapacityList)
    {
        Test_FormatErrorMessage(errorMsg, u8"List's fixed size status is %d, expected %d.",
            IsFixedCapacity,
            shouldBeFixedCapacityList);
        return false;
    }
    bool IsWrapper = WRList_IsWrapperBuffer(list);
    if (IsWrapper != shouldBeWrapperList)
    {
        Test_FormatErrorMessage(errorMsg, u8"List's wrapper status is %d, expected %d.",
            IsWrapper,
            shouldBeWrapperList);
        return false;
    }
    if (WRList_GetCapacity(list) != expectedCapacity)
    {
        Test_FormatErrorMessage(errorMsg, u8"List capacity is %zu, expected %zu.", WRList_GetCapacity(list), expectedCapacity);
        return false;
    }

    return true;
}

static bool VerifyListSequence(TestErrorMessage* errorMsg,
    WRList* list,
    const void* expectedElements,
    size_t expectedCount,
    const unsigned char* context)
{
    if (WRList_GetCount(list) != expectedCount)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"List count is %zu, expected %zu. (%s)",
            WRList_GetCount(list), expectedCount, context);
        return false;
    }

    if (!Memory_IsEqual(WRList_GetDataArray(list), expectedElements, expectedCount * WRList_GetElementSize(list)))
    {
        Test_FormatErrorMessage(errorMsg, 
            u8"List's element array does not have the correct contents, but element count matches. (%s)",
            context);
        return false;
    }
    return true;
}


/* Test functions. */
static bool TestSimpleMutation(TestErrorMessage* errorMsg, ErrorMessagePool* errorPool, WRList* list)
{
    /* Lord forgive me for my sins. */
    SIMPLE_MUTATION_ELEMENT Arr1[] = { 1 };
    Error Result = WRList_AddFirst(list, &Arr1[0]);
    if (Result.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg, u8"Error adding element to list: %s", ErrorMessageOrDefault(Result.Message));
        return false;
    }
    if (!VerifyListSequence(errorMsg, list, Arr1, sizeof(Arr1) / sizeof(Arr1[0]), u8"After adding dummy first element in simple mutation."))
    {
        return false;
    }

    SIMPLE_MUTATION_ELEMENT Arr2[] = { 0, 1 };
    Result = WRList_AddFirst(list, &Arr2[0]);
    if (Result.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg, u8"Error prepending element to list: %s", ErrorMessageOrDefault(Result.Message));
        return false;
    }
    if (!VerifyListSequence(errorMsg, list, Arr2,  sizeof(Arr2) / sizeof(Arr2[0]), u8"Prepend second element."))
    {
        return false;
    }

    SIMPLE_MUTATION_ELEMENT Arr3[] = { 0, 1, 2 };
    Result = WRList_AddLast(list, &Arr3[2]);
    if (Result.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg, u8"Error appending element to list: %s", ErrorMessageOrDefault(Result.Message));
        return false;
    }
    if (!VerifyListSequence(errorMsg, list, Arr3,  sizeof(Arr3) / sizeof(Arr3[0]), u8"Appending third element."))
    {
        return false;
    }

    Result = WRList_Insert(list, &Arr3[2], 9999);
    if (Result.Code != ErrorCode_IndexOutOfBounds)
    {
        Test_FormatErrorMessage(errorMsg, u8"Expected index out of bounds error inserting element, got code %d: %s",
            Result.Code, ErrorMessageOrDefault(Result.Message));
        return false;
    }
    ErrorMessagePool_Clear(errorPool);


    SIMPLE_MUTATION_ELEMENT Arr4[] = { 0, 5, 1, 2 };
    Result = WRList_Insert(list, &Arr4[1], 1);
    if (Result.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg, u8"Error inserting element in list: %s", ErrorMessageOrDefault(Result.Message));
        return false;
    }
    if (!VerifyListSequence(errorMsg, list, Arr4,  sizeof(Arr4) / sizeof(Arr4[0]), u8"Inserting fourth element."))
    {
        return false;
    }

    SIMPLE_MUTATION_ELEMENT Arr5[] = { 0, 5, 1, 2, 51, 52 };
    Result = WRList_AppendRange(list, &Arr5[4], 2);
    if (Result.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg, u8"Error appending range: %s", ErrorMessageOrDefault(Result.Message));
        return false;
    }
    if (!VerifyListSequence(errorMsg, list, Arr5,  sizeof(Arr5) / sizeof(Arr5[0]), u8"Appending range."))
    {
        return false;
    }

    SIMPLE_MUTATION_ELEMENT Arr6[] = { 99, 98, 0, 5, 1, 2, 51, 52 };
    Result = WRList_PrependRange(list, &Arr6[0], 2);
    if (Result.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg, u8"Error prepending range: %s", ErrorMessageOrDefault(Result.Message));
        return false;
    }
    if (!VerifyListSequence(errorMsg, list, Arr6, sizeof(Arr6) / sizeof(Arr6[0]), u8"Prepending range."))
    {
        return false;
    }

    Result = WRList_InsertRange(list, &Arr6[0], 1,  9999);
    if (Result.Code != ErrorCode_IndexOutOfBounds)
    {
        Test_FormatErrorMessage(errorMsg, u8"Expected index out of bounds range, got code %d: %s",
            Result.Code, ErrorMessageOrDefault(Result.Message));
        return false;
    }
    ErrorMessagePool_Clear(errorPool);

    SIMPLE_MUTATION_ELEMENT Arr7[] = { 99, 98, 25, 56, 0, 5, 1, 2, 51, 52 };
    Result = WRList_InsertRange(list, &Arr7[2], 2,  2);
    if (Result.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg, u8"Error inserting range: %s", ErrorMessageOrDefault(Result.Message));
        return false;
    }
    if (!VerifyListSequence(errorMsg, list, Arr7, sizeof(Arr7) / sizeof(Arr7[0]), u8"Inserting range."))
    {
        return false;
    }

    SIMPLE_MUTATION_ELEMENT Arr8[] = { 98, 25, 56, 0, 5, 1, 2, 51, 52 };
    Result = WRList_RemoveFirst(list);
    if (Result.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg, u8"Error removing first element: %s", ErrorMessageOrDefault(Result.Message));
        return false;
    }
    if (!VerifyListSequence(errorMsg, list, Arr8, sizeof(Arr8) / sizeof(Arr8[0]), u8"Removing first element."))
    {
        return false;
    }

    SIMPLE_MUTATION_ELEMENT Arr9[] = { 98, 25, 56, 0, 5, 1, 2, 51 };
    Result = WRList_RemoveLast(list);
    if (Result.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg, u8"Error removing last element: %s", ErrorMessageOrDefault(Result.Message));
        return false;
    }
    if (!VerifyListSequence(errorMsg, list, Arr9, sizeof(Arr9) / sizeof(Arr9[0]), u8"Removing last element."))
    {
        return false;
    }

    Result = WRList_RemoveRange(list, 1, 9999);
    if (Result.Code != ErrorCode_IndexOutOfBounds)
    {
        Test_FormatErrorMessage(errorMsg, u8"Expected index out of bounds error removing range, got code %d: %s",
            Result.Code, ErrorMessageOrDefault(Result.Message));
        return false;
    }
    ErrorMessagePool_Clear(errorPool);

    Result = WRList_RemoveRange(list, 10, 1);
    if (Result.Code != ErrorCode_IllegalArgument)
    {
        Test_FormatErrorMessage(errorMsg, u8"Expected illegal argument error removing range, got code %d: %s",
            Result.Code, ErrorMessageOrDefault(Result.Message));
        return false;
    }
    ErrorMessagePool_Clear(errorPool);

    SIMPLE_MUTATION_ELEMENT Arr10[] = { 98, 51 };
    Result = WRList_RemoveRange(list, 1, 7);
    if (Result.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg, u8"Error removing range: %s", ErrorMessageOrDefault(Result.Message));
        return false;
    }
    if (!VerifyListSequence(errorMsg, list, Arr10, sizeof(Arr10) / sizeof(Arr10[0]), u8"Removing range."))
    {
        return false;
    }

    WRList_Clear(list);
    if (WRList_GetCount(list) != 0)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"Expected list to be empty after clearing it, instead got %zu elements.",
            WRList_GetCount(list));
        return false;
    }

    return true;
}

static bool ComparseSingleElement(const void* element1,
    const void* element2,
    Error errorResult,
    TestErrorMessage* errorMsg,
    size_t elementSize)
{
    if (errorResult.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"Error retrieving first element of list: %s",
            ErrorMessageOrDefault(errorResult.Message));
        return false;
    }
    if (!Memory_IsEqual(element1, element2, elementSize))
    {
        Test_FormatErrorMessage(errorMsg,
            u8"Elements in single element compare don't match: %s",
            ErrorMessageOrDefault(errorResult.Message));
        return false;
    }

    return true;
}

static bool AccessorNumberPredicate(WRList* self, WRListElementData element, void* userData)
{
    UNUSED(self);
    ACCESSORS_ELEMENT TargetElement = *((ACCESSORS_ELEMENT*)userData);
    ACCESSORS_ELEMENT ElementValue = *((ACCESSORS_ELEMENT*)element._element);
    return ElementValue == TargetElement;
}


static bool TestAccessors(TestErrorMessage* errorMsg, ErrorMessagePool* errorPool, WRList* list)
{
    UNUSED(errorPool);

    ACCESSORS_ELEMENT Arr1[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    Error Result = WRList_AppendRange(list, Arr1, sizeof(Arr1) / sizeof(Arr1[0]));
    if (Result.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg, u8"Error appending range in accessors test: %s", ErrorMessageOrDefault(Result.Message));
        return false;
    }

    ACCESSORS_ELEMENT OutElement;
    Result = WRList_GetFirst(list, &OutElement);
    if (!ComparseSingleElement(&OutElement, &Arr1[0], Result, errorMsg, sizeof(OutElement)))
    {
        return false;
    }

    Result = WRList_GetLast(list, &OutElement);
    if (!ComparseSingleElement(&OutElement, &Arr1[9], Result, errorMsg, sizeof(OutElement)))
    {
        return false;
    }

    size_t TargetIndex = 5;
    Result = WRList_GetAt(list, TargetIndex, &OutElement);
    if (!ComparseSingleElement(&OutElement, &Arr1[TargetIndex], Result, errorMsg, sizeof(OutElement)))
    {
        return false;
    }

    void* OutElementPtr;
    Result = WRList_GetPointerToFirst(list, &OutElementPtr);
    if (!ComparseSingleElement(OutElementPtr, &Arr1[0], Result, errorMsg, sizeof(OutElement)))
    {
        return false;
    }

    Result = WRList_GetPointerToLast(list, &OutElementPtr);
    if (!ComparseSingleElement(OutElementPtr, &Arr1[9], Result, errorMsg, sizeof(OutElement)))
    {
        return false;
    }

    Result = WRList_GetPointerToElement(list, TargetIndex, &OutElementPtr);
    if (!ComparseSingleElement(OutElementPtr, &Arr1[TargetIndex], Result, errorMsg, sizeof(OutElement)))
    {
        return false;
    }

    ACCESSORS_ELEMENT TargetElement = 3;
    if (!WRList_Contains(list, &AccessorNumberPredicate, &TargetElement))
    {
        Test_FormatErrorMessage(errorMsg, u8"List contains call returned false when it should've returned true.");
        return false;
    }

    size_t OutIndex;
    TargetIndex = 2;
    if (!WRList_FirstIndexOf(list, &AccessorNumberPredicate, &TargetElement, &OutIndex))
    {
        Test_FormatErrorMessage(errorMsg, u8"List first index of call returned false when it should've returned true.");
        return false;
    }
    if (OutIndex != TargetIndex)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"List first index of call returned wrong index of %zu, expected %zu.",
            OutIndex, TargetIndex);
        return false;
    }

    TargetElement = 6;
    TargetIndex = 5;
    if (!WRList_LastIndexOf(list, &AccessorNumberPredicate, &TargetElement, &OutIndex))
    {
        Test_FormatErrorMessage(errorMsg, u8"List last index of call returned false when it should've returned true.");
        return false;
    }
    if (OutIndex != TargetIndex)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"List last index of call returned wrong index of %zu, expected %zu.",
            OutIndex, TargetIndex);
        return false;
    }

    return true;
}

static bool TestSpecialMutation(TestErrorMessage* errorMsg, ErrorMessagePool* errorPool, WRList* list)
{
    UNUSED(errorPool);

    SPECIAL_MUTATION_ELEMENT Arr1[] = { 1, 2, 3, 4, 5 };
    Error Result = WRList_AppendRange(list, Arr1, sizeof(Arr1) / sizeof(Arr1[0]));
    if (Result.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg, u8"Error appending range in special mutation test: %s", ErrorMessageOrDefault(Result.Message));
        return false;
    }

    SPECIAL_MUTATION_ELEMENT Arr2[] = { 1, 2, 9, 4, 5 };
    Result = WRList_Replace(list, &Arr2[2], 2);
    if (Result.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg, u8"Error replacing element: %s", ErrorMessageOrDefault(Result.Message));
        return false;
    }
    if (!VerifyListSequence(errorMsg, list, Arr2, sizeof(Arr2) / sizeof(Arr2[0]), u8"Replacing element."))
    {
        return false;
    }

    SPECIAL_MUTATION_ELEMENT Arr3[] = { 5, 2, 9, 4, 1 };
    Result = WRList_Swap(list, 0, 4);
    if (Result.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg, u8"Error swapping element: %s", ErrorMessageOrDefault(Result.Message));
        return false;
    }
    if (!VerifyListSequence(errorMsg, list, Arr3, sizeof(Arr3) / sizeof(Arr3[0]), u8"Swapping element."))
    {
        return false;
    }

    return true;
}

static bool TestTechnicalFunctions(TestErrorMessage* errorMsg, ErrorMessagePool* errorPool, WRList* list)
{
    UNUSED(errorPool);

    if (WRList_GetCapacityRemaining(list) != 0)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"Expected capacity remaining to be 0 after construction, got %zu.",
            WRList_GetCapacityRemaining(list));
        return false;
    }
    if (WRList_IsFixedCapacity(list))
    {
        Test_FormatErrorMessage(errorMsg, u8"Expected list to have non-fixed capacity.");
        return false;
    }
    if (WRList_IsWrapperBuffer(list))
    {
        Test_FormatErrorMessage(errorMsg, u8"Expected list to not be a buffer wrapper.");
        return false;
    }

    size_t EnsuredCapacity = 10;
    WRList_EnsureCapacity(list, EnsuredCapacity);
    if (WRList_GetCapacityRemaining(list) < EnsuredCapacity)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"List only ensured capacity of %zu, expected at least %zu.",
            WRList_GetCapacityRemaining(list), EnsuredCapacity);
        return false;
    }

    size_t RequestedExtraCapacity = 16;
    WRList_ReserveSpace(list, WRList_GetCapacity(list) - EnsuredCapacity + RequestedExtraCapacity);
    if (WRList_GetCapacityRemaining(list) - EnsuredCapacity < RequestedExtraCapacity)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"List only reserved capacity of %zu, expected at least %zu.",
            WRList_GetCapacityRemaining(list), EnsuredCapacity + RequestedExtraCapacity);
        return false;
    }

    return true;
}

static bool IsDoubleListSorted(TestErrorMessage* errorMsg, WRList* list, int32_t direction, const unsigned char* context)
{
    for (size_t i = 1; i < WRList_GetCount(list); i++)
    {
        double ElPrev, ElCurrent;
        Error Result = WRList_GetAt(list, i - 1, &ElPrev);
        if (Result.Code != ErrorCode_Success)
        {
            Test_FormatErrorMessage(errorMsg, 
                u8"Error retrieving previous element in list sort test (%s): %s",
                context, ErrorMessageOrDefault(Result.Message));
            return false;
        }
        Result = WRList_GetAt(list, i, &ElCurrent);
        if (Result.Code != ErrorCode_Success)
        {
            Test_FormatErrorMessage(errorMsg, 
                u8"Error retrieving current element in list sort test (%s): %s",
                context, ErrorMessageOrDefault(Result.Message));
            return false;
        }

        if ((ElCurrent - ElPrev) * direction < 0.0)
        {
            Test_FormatErrorMessage(errorMsg, u8"List is not correctly sorted (%s).", context);
            return false;
        }
    }
    return true;
}

static bool TestAlgorithms(TestErrorMessage* errorMsg, ErrorMessagePool* errorPool, WRList* list)
{
    UNUSED(errorPool);

    SPECIAL_MUTATION_ELEMENT Arr1[] = { 1.0, 3.0, 2.0, 5.0, 4.0, 7.0, 6.0 };
    const size_t ElementCount = sizeof(Arr1) / sizeof(Arr1[0]);
    Error Result = WRList_AppendRange(list, Arr1, ElementCount);
    if (Result.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg, u8"Error appending range in algorithm test: %s", ErrorMessageOrDefault(Result.Message));
        return false;
    }

    WRList_SortAscending(list, &WRList_CompareDouble, NULL);
    if (!IsDoubleListSorted(errorMsg, list, 1, u8"ascending"))
    {
        return false;
    }

    WRList_SortDescending(list, &WRList_CompareDouble, NULL);
    if (!IsDoubleListSorted(errorMsg, list, -1, u8"descending"))
    {
        return false;
    }

    return true;
}

static bool TransformationFilterPredicate(WRList* self, WRListElementData element, void* userData)
{
    UNUSED(self);
    UNUSED(userData);
    TRANSFORMATION_ELEMENT Value = *((TRANSFORMATION_ELEMENT*)element._element);
    return Value % 2 == 0;
}

static void TransformationMapToSelfFunc(WRList* self, WRListElementData sourceEl, void* destElement, void* userData)
{
    UNUSED(self);
    TRANSFORMATION_ELEMENT Multiplier = *((TRANSFORMATION_ELEMENT*)userData);
    TRANSFORMATION_ELEMENT Value = *((TRANSFORMATION_ELEMENT*)sourceEl._element);
    TRANSFORMATION_ELEMENT Result = Value * Multiplier;
    Memory_Copy(&Result, destElement, sizeof(Result));
}

static void TransformationMapFunc(WRList* self, WRListElementData sourceEl, void* destElement, void* userData)
{
    UNUSED(self);
    UNUSED(userData);
    TRANSFORMATION_ELEMENT Value = *((TRANSFORMATION_ELEMENT*)sourceEl._element);
    TRANSFORMATION_DEST_ELEMENT Result = (TRANSFORMATION_DEST_ELEMENT)Value;
    Memory_Copy(&Result, destElement, sizeof(Result));
}

static bool TestTransformations(TestErrorMessage* errorMsg, ErrorMessagePool* errorPool, WRList* list)
{
    TRANSFORMATION_ELEMENT Arr1[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    Error Result = WRList_AppendRange(list, Arr1,  sizeof(Arr1) / sizeof(Arr1[0]));
    if (Result.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg, u8"Error appending range in transformation test: %s", ErrorMessageOrDefault(Result.Message));
        return false;
    }

    TRANSFORMATION_ELEMENT Arr2[] = { 2, 4, 6, 8, 10 };
    WRList_Filter(list, &TransformationFilterPredicate, NULL);
    if (!VerifyListSequence(errorMsg, list, Arr2, sizeof(Arr2) / sizeof(Arr2[0]), u8"list filter"))
    {
        return false;
    }

    TRANSFORMATION_ELEMENT ElBuffer;
    TRANSFORMATION_ELEMENT Multiplier = 2;
    TRANSFORMATION_ELEMENT Arr3[] = { 4, 8, 12, 16, 20 };
    Result = WRList_MapToSelf(list, &TransformationMapToSelfFunc, &ElBuffer, &Multiplier);
    if (Result.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg, u8"Error mapping list to self: %s", ErrorMessageOrDefault(Result.Message));
        return false;
    }
    if (!VerifyListSequence(errorMsg, list, Arr3, sizeof(Arr3) / sizeof(Arr3[0]), u8"list map to self"))
    {
        return false;
    }

    WRList DestList;
    Result = WRList_Construct1(&DestList, sizeof(TRANSFORMATION_DEST_ELEMENT), errorPool);
    if (Result.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg, u8"Error constricting mapping destination list: %s", ErrorMessageOrDefault(Result.Message));
        return false;
    }

    TRANSFORMATION_DEST_ELEMENT ElBufferDest;
    TRANSFORMATION_DEST_ELEMENT ArrDest[] = { 4, 8, 12, 16, 20 };
    Result = WRList_Map(list, &DestList, &TransformationMapFunc, &ElBufferDest, NULL);
    if (Result.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg, u8"Error mapping list: %s", ErrorMessageOrDefault(Result.Message));
        WRList_Deconstruct1(&DestList);
        return false;
    }
    if (!VerifyListSequence(errorMsg, &DestList, ArrDest, sizeof(ArrDest) / sizeof(ArrDest[0]), u8"list map"))
    {
        WRList_Deconstruct1(&DestList);
        return false;
    }

    WRList_Deconstruct1(&DestList);
    return true;
}

static bool FullManipulationCountWherePredicate(WRList* self, WRListElementData element, void* userData)
{
    UNUSED(self);
    FULL_MANIPULATION_ELEMENT TargetValue = *((FULL_MANIPULATION_ELEMENT*)userData);
    FULL_MANIPULATION_ELEMENT Value = *((FULL_MANIPULATION_ELEMENT*)element._element);
    return Value == TargetValue;
}

static bool TestFullManipulation(TestErrorMessage* errorMsg, ErrorMessagePool* errorPool, WRList* list)
{
    UNUSED(errorPool);

    FULL_MANIPULATION_ELEMENT Arr1[] = { 1, 2, 1, 4, 1, 6, 1, 8, 1, 10, 1};
    Error Result = WRList_AppendRange(list, Arr1,  sizeof(Arr1) / sizeof(Arr1[0]));
    if (Result.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"Error appending range in full manipulation test: %s",
            ErrorMessageOrDefault(Result.Message));
        return false;
    }

    FULL_MANIPULATION_ELEMENT TargetElement = 1;
    size_t MatchingCount = WRList_CountWhere(list, &FullManipulationCountWherePredicate, &TargetElement);
    if (MatchingCount != 6)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"List CountWhere call returned wrong count of %zu, expected 6.",
            MatchingCount);
        return false;
    }

    FULL_MANIPULATION_ELEMENT Arr2[] = { 1, 10, 1, 8, 1, 6, 1, 4, 1, 2, 1 };
    WRList_Reverse(list);
    if (!VerifyListSequence(errorMsg, list, Arr2, sizeof(Arr2) / sizeof(Arr2[0]), u8"list reverse"))
    {
        return false;
    }
    return true;
}

static bool TestIntegerNumberOperations(TestErrorMessage* errorMsg, ErrorMessagePool* errorPool, WRList* list)
{
    UNUSED(errorPool);

    int64_t Arr1[] = { -50, 100, 25, 15, 5, 3, 2 };
    Error Result = WRList_AppendRange(list, Arr1,  sizeof(Arr1) / sizeof(Arr1[0]));
    if (Result.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"Error appending range in number operations test: %s",
            ErrorMessageOrDefault(Result.Message));
        return false;
    }

    const int64_t ExpectedSum = 100;
    int64_t Sum = WRList_SumInt(list, &WRList_ExtractIntFromInt64, NULL);
    if (Sum != ExpectedSum)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"List SumInt call returned wrong sum of %" PRId64 ", expected %" PRId64 ".",
            Sum, ExpectedSum);
        return false;
    }

    int64_t MinValue;
    int64_t ExpectedMinValue = -50;
    Result = WRList_MinInt(list, &WRList_ExtractIntFromInt64, &MinValue, NULL);
    if (Result.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"Error retrieving list minimum value: %s",
            ErrorMessageOrDefault(Result.Message));
        return false;
    }
    if (MinValue != ExpectedMinValue)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"List minimum value returned wrong value of %" PRId64 ", expected %" PRId64 ".",
            MinValue, ExpectedMinValue);
        return false;
    }

    int64_t MaxValue;
    int64_t ExpectedMaxValue = 100;
    Result = WRList_MaxInt(list, &WRList_ExtractIntFromInt64, &MaxValue, NULL);
    if (Result.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"Error retrieving list maximum value: %s",
            ErrorMessageOrDefault(Result.Message));
        return false;
    }
    if (MaxValue != ExpectedMaxValue)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"List maximum value returned wrong value of %" PRId64 ", expected %" PRId64 ".",
            MaxValue, ExpectedMaxValue);
        return false;
    }

    return true;
}

static double NumberOperationsAbsDouble(double value)
{
    if (value < 0.0)
    {
        return -value;
    }
    return value;
}

static bool NumberOperationEqualsDouble(double a, double b)
{
    const double MARGIN_OF_ERROR = 0.0001;
    return NumberOperationsAbsDouble(a - b) <= MARGIN_OF_ERROR;
}

static bool TestDoubleNumberOperations(TestErrorMessage* errorMsg, ErrorMessagePool* errorPool, WRList* list)
{
    UNUSED(errorPool);

    double Arr1[] = { -50.0, 100.0, 25.0, 15.0, 5.0, 3.0, 2.0 };
    Error Result = WRList_AppendRange(list, Arr1,  sizeof(Arr1) / sizeof(Arr1[0]));
    if (Result.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"Error appending range in number operations test: %s",
            ErrorMessageOrDefault(Result.Message));
        return false;
    }

    const double ExpectedSum = 100.0;
    double Sum = WRList_SumDouble(list, &WRList_ExtractDoubleFromDouble, NULL);
    if (!NumberOperationEqualsDouble(Sum, ExpectedSum))
    {
        Test_FormatErrorMessage(errorMsg,
            u8"List SumInt call returned wrong sum of %f, expected %f.",
            Sum, ExpectedSum);
        return false;
    }

    double MinValue;
    double ExpectedMinValue = -50.0;
    Result = WRList_MinDouble(list, &WRList_ExtractDoubleFromDouble, &MinValue, NULL);
    if (Result.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"Error retrieving list minimum value: %s",
            ErrorMessageOrDefault(Result.Message));
        return false;
    }
    if (!NumberOperationEqualsDouble(MinValue, ExpectedMinValue))
    {
        Test_FormatErrorMessage(errorMsg,
            u8"List minimum value returned wrong value of %f, expected %f.",
            MinValue, ExpectedMinValue);
        return false;
    }

    double MaxValue;
    double ExpectedMaxValue = 100.0;
    Result = WRList_MaxDouble(list, &WRList_ExtractDoubleFromDouble, &MaxValue, NULL);
    if (Result.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"Error retrieving list maximum value: %s",
            ErrorMessageOrDefault(Result.Message));
        return false;
    }
    if (!NumberOperationEqualsDouble(MaxValue, ExpectedMaxValue))
    {
        Test_FormatErrorMessage(errorMsg,
            u8"List maximum value returned wrong value of %f, expected %f.",
            MaxValue, ExpectedMaxValue);
        return false;
    }

    return true;
}

static bool TestPopOperations(TestErrorMessage* errorMsg, ErrorMessagePool* errorPool, WRList* list)
{
    POP_OPERATION_ELEMENT Arr1[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    Error Result = WRList_AppendRange(list, Arr1,  sizeof(Arr1) / sizeof(Arr1[0]));
    if (Result.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"Error appending range in pop operations test: %s",
            ErrorMessageOrDefault(Result.Message));
        return false;
    }

    POP_OPERATION_ELEMENT ExpectedValue = Arr1[0];
    POP_OPERATION_ELEMENT OutEl;
    POP_OPERATION_ELEMENT Arr2[] = { 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    Result = WRList_PopFirst(list, &OutEl);
    if (Result.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg, u8"Error popping first element: %s", ErrorMessageOrDefault(Result.Message));
        return false;
    }
    if (OutEl != ExpectedValue)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"Popped first element returned wrong value of %d, expected %d.",
            OutEl, ExpectedValue);
        return false;
    }
    if (!VerifyListSequence(errorMsg, list, Arr2, sizeof(Arr2) / sizeof(Arr2[0]), u8"pop first element"))
    {
        return false;
    }

    POP_OPERATION_ELEMENT Arr3[] = { 2, 3, 4, 5, 6, 7, 8, 9 };
    ExpectedValue = Arr1[9];
    Result = WRList_PopLast(list, &OutEl);
    if (Result.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg, u8"Error popping last element: %s", ErrorMessageOrDefault(Result.Message));
        return false;
    }
    if (OutEl != ExpectedValue)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"Popped last element returned wrong value of %d, expected %d.",
            OutEl, ExpectedValue);
        return false;
    }
    if (!VerifyListSequence(errorMsg, list, Arr3, sizeof(Arr3) / sizeof(Arr3[0]), u8"pop last element"))
    {
        return false;
    }

    Result = WRList_PopAt(list, 99999, &OutEl);
    if (Result.Code != ErrorCode_IndexOutOfBounds)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"Popped out of bounds element returned success, expected failure. Code %d, %s",
            Result.Code, ErrorMessageOrDefault(Result.Message));
        return false;
    }
    ErrorMessagePool_Clear(errorPool);

    POP_OPERATION_ELEMENT Arr4[] = { 2, 3, 4, 5, 7, 8, 9 };
    ExpectedValue = Arr1[5];
    Result = WRList_PopAt(list, 4, &OutEl);
    if (Result.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg, u8"Error popping element at index: %s", ErrorMessageOrDefault(Result.Message));
        return false;
    }
    if (OutEl != ExpectedValue)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"Popped element at index returned wrong value of %d, expected %d.",
            OutEl, ExpectedValue);
        return false;
    }
    if (!VerifyListSequence(errorMsg, list, Arr4, sizeof(Arr4) / sizeof(Arr4[0]), u8"pop element at index"))
    {
        return false;
    }

    return true;
}


// Functions.
bool Test_TestListInitialization(TestErrorMessage* errorMsg, void* userData)
{
    ErrorMessagePool* ErrorPool = ((ListTestContext*)userData)->_errorPool;
    size_t ElementSize = sizeof(int16_t);
    WRList List;
    Error ErrorResult = WRList_Construct1(&List, ElementSize, ErrorPool);
    if (!VerifyListProperties(errorMsg, ErrorResult, &List, 0, 0, false, ElementSize, false, false))
    {
        return false;
    }
    WRList_Deconstruct1(&List);

    size_t Capacity = 4;
    ElementSize = sizeof(int32_t);
    ErrorResult = WRList_Construct2(&List, ElementSize, Capacity, ErrorPool);
    if (!VerifyListProperties(errorMsg, ErrorResult, &List, Capacity, 0, true, ElementSize, false, false))
    {
        return false;
    }
    WRList_Deconstruct1(&List);

    const size_t CONSTANT_BUFFER_ELEMENT_COUNT = 8;
    int64_t ConstantBuffer[CONSTANT_BUFFER_ELEMENT_COUNT];
    ElementSize = sizeof(ConstantBuffer[0]);
    size_t PresentElementCount = 2;
    size_t ConstantBufferCapacity = CONSTANT_BUFFER_ELEMENT_COUNT;
    GenericBuffer WrappedBuffer = GenericBuffer_CreateConstant(ConstantBuffer, ConstantBufferCapacity, ElementSize, PresentElementCount);
    ErrorResult = WRList_WrapBuffer(&List, &WrappedBuffer, ErrorPool);
    if (!VerifyListProperties(errorMsg, ErrorResult, &List, ConstantBufferCapacity, PresentElementCount, true, ElementSize, true, true))
    {
        return false;
    }
    WRList_Deconstruct1(&List);
    return true;
}

bool Test_TestListBuffer(TestErrorMessage* errorMsg, void* userData)
{
    ErrorMessagePool* ErrorPool = ((ListTestContext*)userData)->_errorPool;
    const size_t BUFFER_ELEMENT_COUNT = 4;
    int32_t Buffer[BUFFER_ELEMENT_COUNT];
    WRList List;
    GenericBuffer WrappedBuffer = GenericBuffer_CreateConstant(Buffer, BUFFER_ELEMENT_COUNT, sizeof(Buffer[0]), 0);
    Error ErrorResult = WRList_WrapBuffer(&List, &WrappedBuffer, ErrorPool);
    if (ErrorResult.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg, u8"Error wrapping buffer with list: %s", ErrorMessageOrDefault(ErrorResult.Message));
        return false;
    }
    int32_t Item = 5;
    for (size_t i = 0; i < BUFFER_ELEMENT_COUNT; i++)
    {
        ErrorResult = WRList_AddLast(&List, &Item);
        if (ErrorResult.Code != ErrorCode_Success)
        {
            Test_FormatErrorMessage(errorMsg, u8"Unexpected error adding element to list: %s", ErrorMessageOrDefault(ErrorResult.Message));
            return false;
        }
    }
    ErrorResult = WRList_AddLast(&List, &Item);
    if (ErrorResult.Code != ErrorCode_BufferTooSmall)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"Expected buffer too small error when adding element, got error code %d: %s",
            ErrorResult.Code, ErrorMessageOrDefault(ErrorResult.Message));
        return false;
    }
    ErrorMessagePool_Clear(ErrorPool);
    WRList_Deconstruct1(&List);
    return true;
}

bool Test_TestListSimpleMutation(TestErrorMessage* errorMsg, void* userData)
{
    ListOperation Operations[] = { { ._elementSize = sizeof(SIMPLE_MUTATION_ELEMENT), ._operator = &TestSimpleMutation } };
    return ExecuteOperations(Operations, sizeof(Operations), errorMsg, ((ListTestContext*)userData)->_errorPool);
}

bool Test_TestListAccessors(TestErrorMessage* errorMsg, void* userData)
{
    ListOperation Operations[] = { { ._elementSize = sizeof(ACCESSORS_ELEMENT), ._operator = &TestAccessors } };
    return ExecuteOperations(Operations, sizeof(Operations), errorMsg, ((ListTestContext*)userData)->_errorPool);
}

bool Test_TestListSpecialMutation(TestErrorMessage* errorMsg, void* userData)
{
    ListOperation Operations[] = { { ._elementSize = sizeof(SPECIAL_MUTATION_ELEMENT), ._operator = &TestSpecialMutation } };
    return ExecuteOperations(Operations, sizeof(Operations), errorMsg, ((ListTestContext*)userData)->_errorPool);
}

bool Test_TestListTechnicalFunctions(TestErrorMessage* errorMsg, void* userData)
{
    ListOperation Operations[] = { { ._elementSize = sizeof(TECHNICAL_TEST_ELEMENT), ._operator = &TestTechnicalFunctions } };
    return ExecuteOperations(Operations, sizeof(Operations), errorMsg, ((ListTestContext*)userData)->_errorPool);
}

bool Test_TestListAlgorithms(TestErrorMessage* errorMsg, void* userData)
{
    ListOperation Operations[] = { { ._elementSize = sizeof(ALGORITHMS_ELEMENT), ._operator = &TestAlgorithms } };
    return ExecuteOperations(Operations, sizeof(Operations), errorMsg, ((ListTestContext*)userData)->_errorPool);
}

bool Test_TestListTransformations(TestErrorMessage* errorMsg, void* userData)
{
    ListOperation Operations[] = { { ._elementSize = sizeof(TRANSFORMATION_ELEMENT), ._operator = &TestTransformations } };
    return ExecuteOperations(Operations, sizeof(Operations), errorMsg, ((ListTestContext*)userData)->_errorPool);
}

bool Test_TestListFullManipulation(TestErrorMessage* errorMsg, void* userData)
{
    ListOperation Operations[] = { { ._elementSize = sizeof(FULL_MANIPULATION_ELEMENT), ._operator = &TestFullManipulation } };
    return ExecuteOperations(Operations, sizeof(Operations), errorMsg, ((ListTestContext*)userData)->_errorPool);
}

bool Test_TestListNumberOperations(TestErrorMessage* errorMsg, void* userData)
{
    ListOperation Operations[] = 
    {
         { ._elementSize = sizeof(double), ._operator = &TestDoubleNumberOperations },
         { ._elementSize = sizeof(int64_t), ._operator = &TestIntegerNumberOperations }
    };
    return ExecuteOperations(Operations, sizeof(Operations), errorMsg, ((ListTestContext*)userData)->_errorPool);
}

bool Test_TestListPopOperations(TestErrorMessage* errorMsg, void* userData)
{
    ListOperation Operations[] = { { ._elementSize = sizeof(POP_OPERATION_ELEMENT), ._operator = &TestPopOperations } };
    return ExecuteOperations(Operations, sizeof(Operations), errorMsg, ((ListTestContext*)userData)->_errorPool);
}