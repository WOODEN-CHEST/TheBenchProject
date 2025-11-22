#pragma once
#include "EngineTest.h"
#include "WRUnicode.h"


// Types.
typedef struct UnicodeTestContextStruct
{
    UnicodeData* _unicode;
} UnicodeTestContext;



// Functions.
bool Test_TestUnicodeBooleans(TestErrorMessage* errorMsg, void* userData);

bool Test_TestUnicodeCodePointValidation(TestErrorMessage* errorMsg, void* userData);

bool Test_TestUnicodeConversions(TestErrorMessage* errorMsg, void* userData);