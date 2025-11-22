#include "test/EngineTest.h"
#include <stdarg.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include "WRUnicodeLoader.h"
#include "raylib.h"
#include "WREnvironment.h"

#include "test/ErrorTest.h"
#include "test/UnicodeTest.h"

// Fields.
static const size_t MAX_ERROR_MESSAGE_LENGTH = 2 << 14;
static const unsigned char* UNICODE_DATA_FILE_NAME = u8"unicode_data.txt";


// Static functions.
static void ExecuteSingleTest(FILE* outStream,
    const unsigned char* testName,
    void* userData,
    TestErrorMessage* errorMsg,
    TestFunc testFunc)
{
    errorMsg->_message[0] = '\0';
    bool Result = (*testFunc)(errorMsg, userData);
    if (!Result)
    {
        fprintf(outStream, "Test \"%s\" failed: %s\n",
            (char*)testName,
            (errorMsg->_message[0] != '\0') ? (char*)errorMsg->_message : "no further information.");
    }
    else
    {
        fprintf(outStream, "Test \"%s\" passed.\n", (char*)testName);
    }
}

static Error LoadUnicodeData(ErrorMessagePool* errorPool, UnicodeData* unicode)
{
    unsigned char PathBuffer[2<<12];
    const char* WorkingPath = GetApplicationDirectory();

    snprintf((char*)PathBuffer,
        sizeof(PathBuffer),
        "%s%c%s",
        WorkingPath,
        ENVIRONMENT_PATH_SEPARATOR_PRIMARY,
        (char*)UNICODE_DATA_FILE_NAME);

    return UnicodeData_Load(errorPool, PathBuffer, unicode);
}

static void FreeTestResources(TestErrorMessage* errorMsg,
    UnicodeData* unicode,
    ErrorMessagePool* errorPool)
{
    if (errorMsg)
    {
        free(errorMsg->_message);
    }
    if (unicode)
    {
        UnicodeData_Deconstruct(unicode);
    }
    if (errorPool)
    {
        ErrorMessagePool_Deconstruct1(errorPool);
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
        FreeTestResources(&ErrorMsg, NULL, NULL);
        fprintf(OutStream, "Failed to allocate memory for an error message, aborting tests.");
        return;
    }

    ExecuteSingleTest(OutStream, u8"Error Pool", NULL, &ErrorMsg, &Test_TestErrorPool);
    ExecuteSingleTest(OutStream, u8"Error Codes Only", NULL, &ErrorMsg, &Test_TestErrorCodeOnly);
    ExecuteSingleTest(OutStream, u8"Error Messages", NULL, &ErrorMsg, &Test_TestErrorMessages);

    ErrorMessagePool ErrorPool;
    ErrorMessagePool_Construct1(&ErrorPool);
    UnicodeData Unicode;
    Error ErrorResult = LoadUnicodeData(&ErrorPool, &Unicode);
    if (ErrorResult.Code != ErrorCode_Success)
    {
        FreeTestResources(&ErrorMsg, NULL, &ErrorPool);
        fprintf(OutStream, "Failed to read unicode data, cannot do further tests: %s",
            ErrorResult.Message ? (char*)ErrorResult.Message : "No further information.");
        return;
    }

    UnicodeTestContext UnicodeTestUserData = (UnicodeTestContext) { ._unicode = &Unicode };
    ExecuteSingleTest(OutStream, u8"Unicode Booleans", &UnicodeTestUserData, &ErrorMsg, &Test_TestUnicodeBooleans);
    ExecuteSingleTest(OutStream, u8"Unicode Code Point Validation", &UnicodeTestUserData, &ErrorMsg, &Test_TestUnicodeCodePointValidation);
    ExecuteSingleTest(OutStream, u8"Unicode Conversions", &UnicodeTestUserData, &ErrorMsg, &Test_TestUnicodeConversions);
    ExecuteSingleTest(OutStream, u8"Unicode Numeric Values", &UnicodeTestUserData, &ErrorMsg, &Test_TestUnicodeNumericValues);

    FreeTestResources(&ErrorMsg, &Unicode, &ErrorPool);
}

void Test_FormatErrorMessage(TestErrorMessage* msg, const unsigned char* format, ...)
{
    va_list Args;
    va_start(Args, format);
    vsnprintf((char*)msg->_message, msg->_maxMessageLength, (const char*)format, Args);
    va_end(Args);
}