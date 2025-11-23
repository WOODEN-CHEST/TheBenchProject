#include "test/ListTest.h"
#include "WRError.h"
#include "WRList.h"
#include <stddef.h>


// Types.
typedef bool (*ListOperator)(TestErrorMessage* errorMsg, ErrorMessagePool* errorPool, WRList* list);

typedef struct ListOperationStruct
{
    ListOperator _operator;
    size_t _elementSize;
} ListOperation;


// Static functions.
static bool OperateOnNewList(TestErrorMessage* errorMsg,
    ErrorMessagePool* errorPool,
    ListOperator operator,
    size_t elementSize)
{
    WRList List;
    WRList_Construct1(&List, elementSize, errorPool);
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


// Functions.
bool Test_TestListInitialization(TestErrorMessage* errorMsg, void* userData)
{
    
}

bool Test_TestListBuffer(TestErrorMessage* errorMsg, void* userData)
{

}