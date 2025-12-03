#include "test/EngineTest.h"
#include "test/NumberTest.h"
#include "WRNumber.h"
#include "WRError.h"
#include <stdint.h>
#include "limits.h"
#include <string.h>
#include "WRCompile.h"
#include <stdio.h>




// Types.
typedef Error (*IntReadFunc)(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, void* Value);
typedef Error (*IntWriteFunc)(ErrorMessagePool* errorPool, const void* value, int32_t base, bool includePrefix, GenericBuffer* buffer);

typedef struct IntegerTestStruct
{
    StandardIntegers _numberBits;
    bool _isNumberSigned;
    size_t _numberByteSize;

    IntReadFunc _readerFunc;
    IntWriteFunc _writerFunc;

    StandardIntegers _minValue;
    StandardIntegers _maxValue;

    const unsigned char* _testContext;
} IntegerTest;

// Static functions.
static Error WrapInt8FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, void* value)
{
    return Number_Int8FromString(errorPool, str, base, value);
}

static Error WrapInt8ToString(ErrorMessagePool* errorPool, const void* value, int32_t base, bool includePrefix, GenericBuffer* buffer)
{
    return Number_Int8ToString(errorPool, *((const int8_t*)value), base, includePrefix, buffer);
}

static Error WrapUInt8FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, void* value)
{
    return Number_UInt8FromString(errorPool, str, base, value);
}

static Error WrapUInt8ToString(ErrorMessagePool* errorPool, const void* value, int32_t base, bool includePrefix, GenericBuffer* buffer)
{
    return Number_UInt8ToString(errorPool, *((const uint8_t*)value), base, includePrefix, buffer);
}

static Error WrapInt16FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, void* value)
{
    return Number_Int16FromString(errorPool, str, base, value);
}

static Error WrapInt16ToString(ErrorMessagePool* errorPool, const void* value, int32_t base, bool includePrefix, GenericBuffer* buffer)
{
    return Number_Int16ToString(errorPool, *((const int16_t*)value), base, includePrefix, buffer);
}

static Error WrapUInt16FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, void* value)
{
    return Number_UInt16FromString(errorPool, str, base, value);
}

static Error WrapUInt16ToString(ErrorMessagePool* errorPool, const void* value, int32_t base, bool includePrefix, GenericBuffer* buffer)
{
    return Number_UInt16ToString(errorPool, *((const uint16_t*)value), base, includePrefix, buffer);
}

static Error WrapInt32FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, void* value)
{
    return Number_Int32FromString(errorPool, str, base, value);
}

static Error WrapInt32ToString(ErrorMessagePool* errorPool, const void* value, int32_t base, bool includePrefix, GenericBuffer* buffer)
{
    return Number_Int32ToString(errorPool, *((const int32_t*)value), base, includePrefix, buffer);
}

static Error WrapUInt32FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, void* value)
{
    return Number_UInt32FromString(errorPool, str, base, value);
}

static Error WrapUInt32ToString(ErrorMessagePool* errorPool, const void* value, int32_t base, bool includePrefix, GenericBuffer* buffer)
{
    return Number_UInt32ToString(errorPool, *((const uint32_t*)value), base, includePrefix, buffer);
}

static Error WrapInt64FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, void* value)
{
    return Number_Int64FromString(errorPool, str, base, value);
}

static Error WrapInt64ToString(ErrorMessagePool* errorPool, const void* value, int32_t base, bool includePrefix, GenericBuffer* buffer)
{
    return Number_Int64ToString(errorPool, *((const int64_t*)value), base, includePrefix, buffer);
}

static Error WrapUInt64FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, void* value)
{
    return Number_UInt64FromString(errorPool, str, base, value);
}

static Error WrapUInt64ToString(ErrorMessagePool* errorPool, const void* value, int32_t base, bool includePrefix, GenericBuffer* buffer)
{
    return Number_UInt64ToString(errorPool, *((const uint64_t*)value), base, includePrefix, buffer);
}


static bool VerifyReadIntegerValue(TestErrorMessage* errorMsg,
    Error errorResult,
    StandardIntegers value,
    StandardIntegers originalBits,
    const unsigned char* testContext)
{
    if (errorResult.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg, u8"Failed to read integer from string (%s): %s", testContext, errorResult.Message);
        return false;
    }
    if (value.UInt64 != originalBits.UInt64)
    {
        Test_FormatErrorMessage(errorMsg, u8"Written int values do not match at test (%s): %s", testContext, errorResult.Message);
        return false;
    }
    return true;
}

static bool TestIntegerRoundOperation(TestErrorMessage* errorMsg,
    ErrorMessagePool* errorPool,
    IntegerTest* test,
    bool writeWithPrefix,
    int32_t writeBase,
    int32_t readBase)
{
    unsigned char StrBuffer[128];
    GenericBuffer GenericStrBuffer = GenericBuffer_CreateConstant(StrBuffer, sizeof(StrBuffer), sizeof(StrBuffer[0]), 0);
    StandardIntegers ReadValue;

    int8_t A = 3;
    Number_Int8ToString(NULL, A, NUMBER_BASE_10, true, &GenericStrBuffer);

    Error ErrorResult = (*test->_writerFunc)(errorPool, &test->_numberBits, writeBase, writeWithPrefix, &GenericStrBuffer);
    if (ErrorResult.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg, u8"Failed to write integer to string (%s): %s", test->_testContext, ErrorResult.Message);
        return false;
    }

    ErrorResult = (*test->_readerFunc)(errorPool, StrBuffer, readBase, &ReadValue);
    if (!VerifyReadIntegerValue(errorMsg, ErrorResult, ReadValue, test->_numberBits, test->_testContext))
    {
        return false;
    }
    return true;
}

static bool ExecuteSingleIntegerTest(TestErrorMessage* errorMsg, ErrorMessagePool* errorPool, IntegerTest* test)
{
    if (!TestIntegerRoundOperation(errorMsg, errorPool, test, false, NUMBER_BASE_10, NUMBER_BASE_AUTO_DETECT) ||
        !TestIntegerRoundOperation(errorMsg, errorPool, test, false, NUMBER_BASE_16, NUMBER_BASE_AUTO_DETECT) ||
        !TestIntegerRoundOperation(errorMsg, errorPool, test, false, NUMBER_BASE_2, NUMBER_BASE_AUTO_DETECT) ||
        !TestIntegerRoundOperation(errorMsg, errorPool, test, false, NUMBER_BASE_10, NUMBER_BASE_10) ||
        !TestIntegerRoundOperation(errorMsg, errorPool, test, false, NUMBER_BASE_16, NUMBER_BASE_16) ||
        !TestIntegerRoundOperation(errorMsg, errorPool, test, false, NUMBER_BASE_2, NUMBER_BASE_2) ||
        !TestIntegerRoundOperation(errorMsg, errorPool, test, true, NUMBER_BASE_10, NUMBER_BASE_AUTO_DETECT) ||
        !TestIntegerRoundOperation(errorMsg, errorPool, test, true, NUMBER_BASE_16, NUMBER_BASE_AUTO_DETECT) ||
        !TestIntegerRoundOperation(errorMsg, errorPool, test, true, NUMBER_BASE_2, NUMBER_BASE_AUTO_DETECT) ||
        !TestIntegerRoundOperation(errorMsg, errorPool, test, true, NUMBER_BASE_10, NUMBER_BASE_10) ||
        !TestIntegerRoundOperation(errorMsg, errorPool, test, true, NUMBER_BASE_16, NUMBER_BASE_16) ||
        !TestIntegerRoundOperation(errorMsg, errorPool, test, true, NUMBER_BASE_2, NUMBER_BASE_2))
    {
        return false;
    }

    return true;
}

static void GetIntegerFunctions(size_t byteSize, bool isSigned, IntReadFunc* readFunc, IntWriteFunc* writeFunc)
{
    if (isSigned)
    {
        if (byteSize == sizeof(int8_t))
        {
            *readFunc = WrapInt8FromString;
            *writeFunc = WrapInt8ToString;
        }
        else if (byteSize == sizeof(int16_t))
        {
            *readFunc = WrapInt16FromString;
            *writeFunc = WrapInt16ToString;
        }
        else if (byteSize == sizeof(int32_t))
        {
            *readFunc = WrapInt32FromString;
            *writeFunc = WrapInt32ToString;
        }
        else
        {
            *readFunc = WrapInt64FromString;
            *writeFunc = WrapInt64ToString;
        }
    }
    else
    {
        if (byteSize == sizeof(uint8_t))
        {
            *readFunc = WrapUInt8FromString;
            *writeFunc = WrapUInt8ToString;
        }
        else if (byteSize == sizeof(uint16_t))
        {
            *readFunc = WrapUInt16FromString;
            *writeFunc = WrapUInt16ToString;
        }
        else if (byteSize == sizeof(uint32_t))
        {
            *readFunc = WrapUInt32FromString;
            *writeFunc = WrapUInt32ToString;
        }
        else
        {
            *readFunc = WrapUInt64FromString;
            *writeFunc = WrapUInt64ToString;
        }
    }
}


static IntegerTest CreateIntegerTest(StandardIntegers numberBits, 
    bool isNumberSigned,
    size_t numberByteSize,
    const unsigned char* testContext)
{
    const size_t BitsInByte = 8;
    StandardIntegers Min;
    StandardIntegers Max;

    uint64_t AllBits1 = UINT64_MAX;
    uint64_t One = 1;

    if (isNumberSigned)
    {
        Min.UInt64 = (One << ((BitsInByte * numberByteSize) - 1));
        Max.UInt64 = AllBits1 >> (BitsInByte * (sizeof(AllBits1) - numberByteSize));
    }
    else
    {
        Min.UInt64 = 0;
        Max.UInt64 = AllBits1;
    }

    IntWriteFunc WriteFunc;
    IntReadFunc ReadFunc;
    GetIntegerFunctions(numberByteSize, isNumberSigned, &ReadFunc, &WriteFunc);

    return (IntegerTest)
    {
        ._minValue = Min,
        ._maxValue = Max,
        ._numberBits = numberBits,
        ._isNumberSigned = isNumberSigned,
        ._testContext = testContext,
        ._readerFunc = ReadFunc,
        ._writerFunc = WriteFunc,
        ._numberByteSize = numberByteSize
    };
}

static bool TestIntegerNormalCases(TestErrorMessage* errorMsg, ErrorMessagePool* errorPool)
{
    IntegerTest Tests[] =
    {
        CreateIntegerTest((StandardIntegers) { .Int64 = 1 }, true, sizeof(int8_t), u8"int8")
    };

    size_t TestCount = sizeof(Tests) / sizeof(Tests[0]);
    for (size_t i = 0; i < TestCount; i++)
    {
        if (!ExecuteSingleIntegerTest(errorMsg, errorPool, &Tests[i]))
        {
            return false;
        }
    }

    return true;
}

static bool TestIntegerSpecialCases(TestErrorMessage* errorMsg, ErrorMessagePool* errorPool)
{
    UNUSED(errorMsg);
    UNUSED(errorPool);
    return true;
}

static bool TestIntegers(TestErrorMessage* errorMsg, ErrorMessagePool* errorPool)
{
    return TestIntegerNormalCases(errorMsg, errorPool) && TestIntegerSpecialCases(errorMsg, errorPool);
}


// Functions.
bool Test_TestIntegers(TestErrorMessage* errorMsg, void* userData)
{
    ErrorMessagePool* ErrorPool = ((NumberTestContext*)userData)->_errorPool;
    return TestIntegers(errorMsg, ErrorPool);
}

bool Test_TestDecimalFromString(TestErrorMessage* errorMsg, void* userData);

bool Test_TestDecimalToString(TestErrorMessage* errorMsg, void* userData);