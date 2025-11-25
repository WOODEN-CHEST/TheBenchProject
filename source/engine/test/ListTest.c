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


// Static functions.
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

static bool CheckListProperties(TestErrorMessage* errorMsg,
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

    return true;
}

static bool TestSimpleMutation(TestErrorMessage* errorMsg, ErrorMessagePool* errorPool, WRList* list)
{
    SIMPLE_MUTATION_ELEMENT Elements[] = { 1, 2, 3, 4, 5 };

    Error ErrorResult = WRList_AddLast(list, );
}


// Functions.
bool Test_TestListInitialization(TestErrorMessage* errorMsg, void* userData)
{
    ErrorMessagePool* ErrorPool = ((ListTestContext*)userData)->_errorPool;

    size_t ElementSize = sizeof(int16_t);
    WRList List;

    Error ErrorResult = WRList_Construct1(&List, ElementSize, ErrorPool);
    if (!CheckListProperties(errorMsg, ErrorResult, &List, 0, 0, false, ElementSize, false, false))
    {
        return false;
    }
    WRList_Deconstruct1(&List);

    size_t Capacity = 4;
    ElementSize = sizeof(int32_t);
    ErrorResult = WRList_Construct2(&List, ElementSize, Capacity, ErrorPool);
    if (!CheckListProperties(errorMsg, ErrorResult, &List, Capacity, 0, true, ElementSize, false, false))
    {
        return false;
    }
    WRList_Deconstruct1(&List);

    const size_t CONSTANT_BUFFER_ELEMENT_COUNT = 8;
    int64_t ConstantBuffer[CONSTANT_BUFFER_ELEMENT_COUNT];
    ElementSize = sizeof(ConstantBuffer[0]);
    size_t PresentElementCount = 2;
    size_t Capacity = CONSTANT_BUFFER_ELEMENT_COUNT;
    ErrorResult = WRList_WrapConstantBuffer(&List,
        ConstantBuffer,
        PresentElementCount,
        Capacity,
        ElementSize,
        ErrorPool);
    if (!CheckListProperties(errorMsg, ErrorResult, &List, Capacity, PresentElementCount, true, ElementSize, true, true))
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
        Test_FormatErrorMessage(errorMsg,
            u8"Error wrapping buffer with list: %s",
            ErrorMessageOrDefault(ErrorResult.Message));
        return false;
    }

    int32_t Item = 5;
    for (size_t i = 0; i < BUFFER_ELEMENT_COUNT; i++)
    {
        ErrorResult = WRList_AddLast(&List, &Item);
        if (ErrorResult.Code != ErrorCode_Success)
        {
            Test_FormatErrorMessage(errorMsg,
                u8"Unexpected error adding element to list: %s",
                ErrorMessageOrDefault(ErrorResult.Message));
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