#include "test/ErrorTest.h"
#include "WRError.h"
#include <string.h>
#include <stddef.h>



// Static functions.
static bool TestInErrorPoolContext(TestErrorMessage* errorMsg, bool (*testFunc)(TestErrorMessage* errorMsg, ErrorMessagePool* errorPool))
{
    ErrorMessagePool Pool;
    ErrorMessagePool_Construct1(&Pool);
    bool Result = (*testFunc)(errorMsg, &Pool);
    ErrorMessagePool_Deconstruct1(&Pool);
    return Result;
}

static bool TestMessagesInErrroPool(TestErrorMessage* errorMsg,
    ErrorMessagePool* errorPool,
    const unsigned char** messages,
    size_t messageCount)
{
    for (size_t i = 0; i < messageCount; i++)
    {
        const unsigned char* Source = messages[i];
        unsigned char* Dest = ErrorMessagePool_GetNextMessage(errorPool);
        strcpy_s((char*)Dest, MAX_ERROR_MESSAGE_BUFFER_LENGTH, (char*)Source);
        Dest[MAX_ERROR_MESSAGE_BUFFER_LENGTH - 1] = '\0';
        if (strcmp((char*)Dest, (char*)Source))
        {
            Test_FormatErrorMessage(errorMsg,
                u8"Expected string \"%s\" in the error message buffer, got \"%s\".",
                Source, Dest);
            return false;
        }
    }
    return true;
}

static bool TestErrorMessagePool(TestErrorMessage* errorMsg, ErrorMessagePool* errorPool)
{
    if (MAX_ERROR_MESSAGE_BUFFER_LENGTH <= 1)
    {
        Test_FormatErrorMessage(errorMsg, u8"Max error message buffer length must be > 1, it was %zu", MAX_ERROR_MESSAGE_BUFFER_LENGTH);
        return false;
    }

    const unsigned char* MessagesA[] = { u8"a", u8"b", u8"c", u8"d" };
    size_t MessageACount = sizeof(MessagesA) / sizeof(MessagesA[0]);
    const unsigned char* MessagesB[] = { u8"e", u8"f", u8"g", u8"h" };
    size_t MessageBCount = sizeof(MessagesB) / sizeof(MessagesB[0]);

    bool Result = TestMessagesInErrroPool(errorMsg, errorPool, MessagesA, MessageACount);
    if (!Result)
    {
        return false;
    }
    Result = TestMessagesInErrroPool(errorMsg, errorPool, MessagesB, MessageBCount);
    if (!Result)
    {
        return false;
    }

    size_t CombinedCount = MessageACount + MessageBCount;
    if (errorPool->_count != CombinedCount)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"Message count in pool after modifications is %zu, expected %zu.",
            errorPool->_count, CombinedCount);
        return false;
    }
    
    ErrorMessagePool_Clear(errorPool);
    if (errorPool->_count != 0)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"Message count in pool after clearing it is %zu, expected 0.",
            errorPool->_count);
        return false;
    }

    return true;
}

static bool AssertErrorMessageContents(TestErrorMessage* errorMsg,
    Error error,
    ErrorCode expectedCode,
    bool canContainMessage,
    const unsigned char* expectedMessage)
{
    if (error.Code != expectedCode)
    {
        Test_FormatErrorMessage(errorMsg, u8"Expected error code %d, got %d.", expectedCode, error.Code);
        return false;
    }
    if (!canContainMessage && error.Message)
    {
        Test_FormatErrorMessage(errorMsg, u8"Expected no error message, got \"%s\".", error.Message);
        return false;
    }
    if (expectedMessage && strcmp((const char*)error.Message, (const char*)expectedMessage))
    {
        Test_FormatErrorMessage(errorMsg, u8"Expected the error message to be \"%s\", got \"%s\".", expectedMessage, error.Message);
        return false;
    }

    return true;
}

static bool TestErrorCodeOnly(TestErrorMessage* errorMsg, ErrorMessagePool* errorPool)
{
    (void)errorPool;

    ErrorCode TargetErrorCode = ErrorCode_IO;
    if (!AssertErrorMessageContents(errorMsg,  Error_Construct5(TargetErrorCode), TargetErrorCode, false, NULL))
    {
        return false;
    }
    if (!AssertErrorMessageContents(errorMsg,  Error_Construct1(NULL, TargetErrorCode, NULL), TargetErrorCode, false, NULL))
    {
        return false;
    }
    if (!AssertErrorMessageContents(errorMsg,  Error_Construct2(NULL, TargetErrorCode, NULL), TargetErrorCode, false, NULL))
    {
        return false;
    }
    if (!AssertErrorMessageContents(errorMsg,  Error_Construct3(NULL, TargetErrorCode, NULL), TargetErrorCode, false, NULL))
    {
        return false;
    }
    if (!AssertErrorMessageContents(errorMsg,  Error_Construct4(NULL, TargetErrorCode, NULL), TargetErrorCode, false, NULL))
    {
        return false;
    }
    if (!AssertErrorMessageContents(errorMsg,  Error_CreateSuccess(), ErrorCode_Success, false, NULL))
    {
        return false;
    }

    return true;
}

static bool TestErrorMessages(TestErrorMessage* errorMsg, ErrorMessagePool* errorPool)
{
    const unsigned char* Msg1 = u8"abacus";
    const unsigned char* Msg2 = u8"bacillus";
    const unsigned char* Msg3 = u8"bonus %d";
    const unsigned char* Msg4 = u8"cactus %d";
    int Arg1 = 57;
    const unsigned char* FinalMsg3 = u8"bonus 57";
    const unsigned char* FinalMsg4 = u8"cactus 57";


    ErrorCode TargetErrorCode = ErrorCode_IndexOutOfBounds;
    
    Error CreatedError = Error_Construct1(errorPool, TargetErrorCode, Msg1);
    if (!AssertErrorMessageContents(errorMsg, CreatedError, TargetErrorCode, true, Msg1))
    {
        return false;
    }

    CreatedError = Error_Construct2(errorPool, TargetErrorCode, (char*)Msg2);
    if (!AssertErrorMessageContents(errorMsg, CreatedError, TargetErrorCode, true, Msg2))
    {
        return false;
    }

    CreatedError = Error_Construct3(errorPool, TargetErrorCode, Msg3, Arg1);
    if (!AssertErrorMessageContents(errorMsg, CreatedError, TargetErrorCode, true, FinalMsg3))
    {
        return false;
    }

    CreatedError = Error_Construct4(errorPool, TargetErrorCode, (char*)Msg4, Arg1);
    if (!AssertErrorMessageContents(errorMsg, CreatedError, TargetErrorCode, true, FinalMsg4))
    {
        return false;
    }

    return true;
}

// Functions.
bool Test_TestErrorPool(TestErrorMessage* errorMsg, void* userData)
{
    (void)userData;
    return TestInErrorPoolContext(errorMsg, &TestErrorMessagePool);
}

bool Test_TestErrorMessages(TestErrorMessage* errorMsg, void* userData)
{
    (void)userData;
    return TestInErrorPoolContext(errorMsg, &TestErrorMessages);
}

bool Test_TestErrorCodeOnly(TestErrorMessage* errorMsg, void* userData)
{
    (void)userData;
    return TestInErrorPoolContext(errorMsg, &TestErrorCodeOnly);
}