#include "test/ListTest.h"
#include "WRError.h"
#include "WRList.h"
#include <stddef.h>
#include <stdint.h>

#define SIMPLE_MUTATION_ELEMENT int32_t


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
static const unsigned char* ErrorMessageOrDefault(const unsigned char* msg)
{
    return msg ? msg : u8"No further information.";
}

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
    bool IsAllocated = list->_data != NULL;
    if (IsAllocated != shouldBeAllocated)
    {
        Test_FormatErrorMessage(errorMsg, u8"List's allocations status is %d, expected %d.", IsAllocated, shouldBeAllocated);
        return false;
    }
    if (list->_count != expectedElementCount)
    {
        Test_FormatErrorMessage(errorMsg, u8"Element count in list is %zu, expected %zu.", list->_count, expectedElementCount);
        return false;
    }
    if (list->_elementSize != elementSize)
    {
        Test_FormatErrorMessage(errorMsg, u8"Element size in list is %zu, expected %zu.", list->_elementSize, elementSize);
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
    if (list->_capacity != expectedCapacity)
    {
        Test_FormatErrorMessage(errorMsg, u8"List capacity is %zu, expected %zu.", list->_capacity, expectedCapacity);
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
    if (list->_count != expectedCount)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"List count is %zu, expected %zu. (%s)",
            list->_count, expectedCount, context);
        return false;
    }

    if (!Memory_IsEqual(list->_data, expectedElements, expectedCount * list->_elementSize))
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
    if (list->_count != 0)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"Expected list to be empty after clearing it, instead got %zu elements.",
            list->_count);
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
    ErrorResult = WRList_WrapConstantBuffer(&List,
        ConstantBuffer,
        PresentElementCount,
        ConstantBufferCapacity,
        ElementSize,
        ErrorPool);
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
    Error ErrorResult = WRList_WrapConstantBuffer(&List, Buffer, 0, BUFFER_ELEMENT_COUNT, sizeof(Buffer[0]), ErrorPool);
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