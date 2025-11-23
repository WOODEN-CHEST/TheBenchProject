#pragma once
#include "test/EngineTest.h"
#include "WRError.h"


// Types.
typedef struct ListTestContextStruct
{
    ErrorMessagePool* _errorPool;
} ListTestContext;


// Functions.
bool Test_TestListInitialization(TestErrorMessage* errorMsg, void* userData);

bool Test_TestListBuffer(TestErrorMessage* errorMsg, void* userData);

bool Test_TestListSimpleMutation(TestErrorMessage* errorMsg, void* userData);

bool Test_TestListAccessors(TestErrorMessage* errorMsg, void* userData);

bool Test_TestListTechnicalFunctions(TestErrorMessage* errorMsg, void* userData);

bool Test_TestListAlgorithms(TestErrorMessage* errorMsg, void* userData);

bool Test_TestListTransformations(TestErrorMessage* errorMsg, void* userData);

bool Test_TestListFullManipulation(TestErrorMessage* errorMsg, void* userData);

bool Test_TestListNumberOperations(TestErrorMessage* errorMsg, void* userData);