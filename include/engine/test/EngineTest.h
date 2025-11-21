#pragma once
#include <stddef.h>


// Types.
typedef struct TestErrorMessageStruct
{
    unsigned char* _message;
    size_t _maxMessageLength;
} TestErrorMessage;

typedef bool (*TestFunc)(TestErrorMessage* errorMsg, void* userData);


// Functions.
void Test_ExecuteEngineTest(void);

void Test_FormatErrorMessage(TestErrorMessage* msg, const unsigned char* format, ...);