#pragma once
#include "test/EngineTest.h"
#include "WRNumber.h"
#include "WRError.h"


// Types.
typedef struct NumberTestContextStruct
{
    ErrorMessagePool* _errorPool;
} NumberTestContext;


// Functions.
bool Test_TestIntegers(TestErrorMessage* errorMsg, void* userData);

bool Test_TestDecimalFromString(TestErrorMessage* errorMsg, void* userData);

bool Test_TestDecimalToString(TestErrorMessage* errorMsg, void* userData);