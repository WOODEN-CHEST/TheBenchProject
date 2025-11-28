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
#include "test/ListTest.h"


// Types.
typedef struct SingleTestStruct
{
    const unsigned char* _name;
    TestFunc _function;
    void* _userData;
} SingleTest;


// Fields.
static const size_t MAX_ERROR_MESSAGE_LENGTH = 2 << 14;
static const unsigned char* UNICODE_DATA_FILE_NAME = u8"unicode_data.txt";


// Static functions.
static bool ExecuteSingleTest(FILE* outStream,
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
    return Result;
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
    SetTraceLogLevel(LOG_WARNING);

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
    ListTestContext ListTestUserData = (ListTestContext) { ._errorPool = &ErrorPool };

    SingleTest Tests[] = 
    {
        {
            ._function = &Test_TestErrorPool,
            ._name = u8"Error Pool",
            ._userData = NULL
        },
        {
            ._function = &Test_TestErrorCodeOnly,
            ._name = u8"Error Codes Only",
            ._userData = NULL
        },
        {
            ._function = &Test_TestErrorMessages,
            ._name = u8"Error Messages",
            ._userData = NULL
        },

        {
            ._function = &Test_TestUnicodeBooleans,
            ._name = u8"Unicode Booleans",
            ._userData = &UnicodeTestUserData
        },
        {
            ._function = &Test_TestUnicodeCodePointValidation,
            ._name = u8"Unicode Code Point Validation",
            ._userData = &UnicodeTestUserData
        },        
        {
            ._function = &Test_TestUnicodeConversions,
            ._name = u8"Unicode Conversions",
            ._userData = &UnicodeTestUserData
        },
        {
            ._function = &Test_TestUnicodeNumericValues,
            ._name = u8"Unicode Numeric Values",
            ._userData = &UnicodeTestUserData
        },

        {
            ._function = &Test_TestListInitialization,
            ._name = u8"List Initialization",
            ._userData = &ListTestUserData
        },
        {
            ._function = &Test_TestListBuffer,
            ._name = u8"List Buffer",
            ._userData = &ListTestUserData
        },
        {
            ._function = &Test_TestListSimpleMutation,
            ._name = u8"List Simple Mutation",
            ._userData = &ListTestUserData
        },
        // {
        //     ._function = &Test_TestListAccessors,
        //     ._name = u8"List Accessors",
        //     ._userData = &ListTestUserData
        // },
        // {
        //     ._function = &Test_TestListTechnicalFunctions,
        //     ._name = u8"List Technical Functions",
        //     ._userData = &ListTestUserData
        // },
        // {
        //     ._function = &Test_TestListAlgorithms,
        //     ._name = u8"List Algorithms",
        //     ._userData = &ListTestUserData
        // },
        // {
        //     ._function = &Test_TestListTransformations,
        //     ._name = u8"List Transformations",
        //     ._userData = &ListTestUserData
        // },
        // {
        //     ._function = &Test_TestListFullManipulation,
        //     ._name = u8"List Full Manipulation",
        //     ._userData = &ListTestUserData
        // },
        // {
        //     ._function = &Test_TestListNumberOperations,
        //     ._name = u8"List Number Operations",
        //     ._userData = &ListTestUserData
        // },
        // {
        //     ._function = &Test_TestListPopOperations,
        //     ._name = u8"List Pop Operations",
        //     ._userData = &ListTestUserData
        // },
        // {
        //     ._function = &Test_TestListBufferConversion,
        //     ._name = u8"List Buffer Conversion",
        //     ._userData = &ListTestUserData
        // }
    };

    size_t PassedTestCount = 0;
    size_t TestCount = sizeof(Tests) / sizeof(Tests[0]);
    for (size_t i = 0; i < TestCount; i++)
    {
        SingleTest* TargetTest = &Tests[i];
        if (ExecuteSingleTest(OutStream, TargetTest->_name, TargetTest->_userData, &ErrorMsg, TargetTest->_function))
        {
            PassedTestCount++;
        }
        ErrorMessagePool_Clear(&ErrorPool);
    }

    printf("%zu/%zu tests passed.\n", PassedTestCount, TestCount);

    FreeTestResources(&ErrorMsg, &Unicode, &ErrorPool);
}

void Test_FormatErrorMessage(TestErrorMessage* msg, const unsigned char* format, ...)
{
    va_list Args;
    va_start(Args, format);
    vsnprintf((char*)msg->_message, msg->_maxMessageLength, (const char*)format, Args);
    va_end(Args);
}