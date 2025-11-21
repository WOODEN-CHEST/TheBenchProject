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
void ExecuteEngineTest(void);

void FormatErrorMessage(TestErrorMessage* msg, const unsigned char* format, ...);