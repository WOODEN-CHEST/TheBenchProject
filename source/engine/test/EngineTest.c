#include "test/EngineTest.h"
#include <stdarg.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>

#include "test/ErrorTest.h"

// Fields.
static const size_t MAX_ERROR_MESSAGE_LENGTH = 2 << 14;


// Static functions.
static void ExecuteSingleTest(FILE* outStream,
    const unsigned char* testName,
    void* userData,
    TestErrorMessage* errorMsg,
    TestFunc testFunc)
{
    bool Result = (*testFunc)(errorMsg, userData);
    if (!Result)
    {
        fprintf(outStream, "Test \"%s\" failed: %s\n",
            (char*)testName,
            errorMsg->_message ? (char*)errorMsg->_message : "no further information.");
    }
    else
    {
        fprintf(outStream, "Test \"%s\" passed.\n", (char*)testName);
    }
}


// Functions
void Test_ExecuteEngineTest()
{
    FILE* OutStream = stdout;

    TestErrorMessage ErrorMsg;
    ErrorMsg._maxMessageLength = MAX_ERROR_MESSAGE_LENGTH;
    ErrorMsg._message = malloc(MAX_ERROR_MESSAGE_LENGTH);
    if (!ErrorMsg._message)
    {
        fprintf(OutStream, "Failed to allocate memory for an error message, aborting tests.");
        return;
    }

    ExecuteSingleTest(OutStream, u8"Error Pool", NULL, &ErrorMsg, &Test_TestErrorPool);
    ExecuteSingleTest(OutStream, u8"Error Codes Only", NULL, &ErrorMsg, &Test_TestErrorCodeOnly);
    ExecuteSingleTest(OutStream, u8"Error Messages", NULL, &ErrorMsg, &Test_TestErrorMessages);
}

void Test_FormatErrorMessage(TestErrorMessage* msg, const unsigned char* format, ...)
{
    va_list Args;
    va_start(Args, format);
    vsnprintf((char*)msg->_message, msg->_maxMessageLength, (const char*)format, Args);
    va_end(Args);
}