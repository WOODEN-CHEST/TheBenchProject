#include "test/EngineTest.h"
#include "test/NumberTest.h"
#include "WRNumber.h"
#include "WRError.h"
#include <stdint.h>
#include "limits.h"
#include <string.h>
#include "WRCompile.h"
#include <stdio.h>
#include <WRMemory.h>
#include <inttypes.h>




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
    size_t numberByteSize,
    const unsigned char* testContext,
    const unsigned char* sourceStr)
{
    if (errorResult.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"Failed to read integer from string \"%s\" (%s): %s",
            sourceStr, testContext, errorResult.Message);
        return false;
    }
    if (!Memory_IsEqual(&value, &originalBits, numberByteSize))
    {
        Test_FormatErrorMessage(errorMsg,
            u8"Written int values do not match at test for str \"%s\" (%s). Written bits: %" PRIu64 ", original bits: %" PRIu64 ".",
            sourceStr, testContext, value.UInt64, originalBits.UInt64);
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
    Memory_Zero(&ReadValue, sizeof(ReadValue));

    Error ErrorResult = (*test->_writerFunc)(errorPool, &test->_numberBits, writeBase, writeWithPrefix, &GenericStrBuffer);
    if (ErrorResult.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg, u8"Failed to write integer to string (%s): %s", test->_testContext, ErrorResult.Message);
        return false;
    }

    ErrorResult = (*test->_readerFunc)(errorPool, StrBuffer, readBase, &ReadValue);
    if (!VerifyReadIntegerValue(errorMsg, ErrorResult, ReadValue, test->_numberBits,
        test->_numberByteSize, test->_testContext, GenericStrBuffer._data))
    {
        return false;
    }
    return true;
}

static bool ExecuteSingleIntegerTest(TestErrorMessage* errorMsg, ErrorMessagePool* errorPool, IntegerTest* test)
{
    if (!TestIntegerRoundOperation(errorMsg, errorPool, test, false, NUMBER_BASE_10, NUMBER_BASE_AUTO_DETECT) ||
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
        CreateIntegerTest((StandardIntegers) { .Int8 = INT8_MAX }, true, sizeof(int8_t), u8"int8 max"),
        CreateIntegerTest((StandardIntegers) { .Int8 = INT8_MIN }, true, sizeof(int8_t), u8"int8 min"),
        CreateIntegerTest((StandardIntegers) { .UInt8 = UINT8_MAX }, false, sizeof(uint8_t), u8"uint8 max"),

        CreateIntegerTest((StandardIntegers) { .Int16 = INT16_MAX }, true, sizeof(int16_t), u8"int16 max"),
        CreateIntegerTest((StandardIntegers) { .Int16 = INT16_MIN }, true, sizeof(int16_t), u8"int16 min"),
        CreateIntegerTest((StandardIntegers) { .UInt16 = UINT16_MAX }, false, sizeof(uint16_t), u8"uint16 max"),
    
        CreateIntegerTest((StandardIntegers) { .Int32 = INT32_MAX }, true, sizeof(int32_t), u8"int32 max"),
        CreateIntegerTest((StandardIntegers) { .Int32 = INT32_MIN }, true, sizeof(int32_t), u8"int32 min"),
        CreateIntegerTest((StandardIntegers) { .UInt32 = UINT32_MAX }, false, sizeof(uint32_t), u8"uint32 max"),
    
        CreateIntegerTest((StandardIntegers) { .Int64 = INT64_MAX }, true, sizeof(int64_t), u8"int64 max"),
        CreateIntegerTest((StandardIntegers) { .Int64 = INT64_MIN }, true, sizeof(int64_t), u8"int64 min"),
        CreateIntegerTest((StandardIntegers) { .UInt64 = UINT64_MAX }, false, sizeof(uint64_t), u8"uint64 max"),
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

static bool VerifyErrorCodeFromIntegerParse(TestErrorMessage* errorMsg,
    ErrorMessagePool* errorPool,
    Error error,
    ErrorCode targetCode,
    const unsigned char* strToParse)
{
    if (error.Code != targetCode)
    {
        Test_FormatErrorMessage(errorMsg, 
            u8"Expected error code %d parsing string \"%s\", got code %d: %s",
            targetCode, strToParse, error.Code, ErrorMessageOrDefault(error.Message));
        return false;
    }
    ErrorMessagePool_Clear(errorPool);
    return true;
}

static bool VerifyThatIntParsingFails(TestErrorMessage* errorMsg, ErrorMessagePool* errorPool, const unsigned char* str)
{
    StandardIntegers ParsedResult;

    Memory_Zero(&ParsedResult, sizeof(ParsedResult));
    Error Result = Number_Int64FromString(errorPool, str, NUMBER_BASE_AUTO_DETECT, &ParsedResult.Int64);
    if (!VerifyErrorCodeFromIntegerParse(errorMsg, errorPool, Result, ErrorCode_IllegalArgument, str))
    {
        return false;
    }
    return true;
}

static bool TestIntegerSpecialCases(TestErrorMessage* errorMsg, ErrorMessagePool* errorPool)
{
    const unsigned char* FailCases[] = {    
        u8"", u8" ", u8"\t", u8"\n", u8" 1", u8"1 ", u8"\t1", u8"1\t",
        u8"k", u8"+", u8"-", u8"+-", u8"-+", u8"--", u8"1-", u8"1+2", u8"1-2",
        u8"0x", u8"0b", u8"0x ", u8"0b ", u8"0xg", u8"0x1g3", u8"0ba", u8"0b102",
        u8"0b0x1", u8"0x0x1", u8"0b0b1", u8"0x+1", u8"0b-1", u8"10x1",
        u8"g", u8"1_2", u8"999999999999999999999999999999",
        u8"-1u", u8"++", u8"-", u8"+1-", u8"1 2"
    };

    for (size_t i = 0; i < (sizeof(FailCases) / sizeof(FailCases[0])); i++)
    {
        if (!VerifyThatIntParsingFails(errorMsg, errorPool, FailCases[i]))
        {
            return false;
        }
    }

    const int32_t InvalidBase = NUMBER_BASE_MAX + 1;
    StandardIntegers ParsedResult;
    Error Result = Number_Int64FromString(errorPool, u8"1", InvalidBase, &ParsedResult.Int64);
    if (Result.Code != ErrorCode_IllegalArgument)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"Expected illegal argument error after invalid base, got code %d: %s",
            Result.Code, ErrorMessageOrDefault(Result.Message));
        return false;
    }
    ErrorMessagePool_Clear(errorPool);

    unsigned char StrBuffer[128];
    GenericBuffer StrGenericBuffer = GenericBuffer_CreateConstant(StrBuffer, sizeof(StrBuffer), sizeof(StrBuffer[0]), 0);
    Result = Number_Int64ToString(errorPool, 0, NUMBER_BASE_AUTO_DETECT, false, &StrGenericBuffer);
    if (Result.Code != ErrorCode_IllegalArgument)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"Expected illegal argument after passing base auto detect to a tostring function, got code %d: %s",
            Result.Code, ErrorMessageOrDefault(Result.Message));
        return false;
    }

    Result = Number_Int64ToString(errorPool, 0, InvalidBase, false, &StrGenericBuffer);
    if (Result.Code != ErrorCode_IllegalArgument)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"Expected illegal argument after passing invalid base to a tostring function, got code %d: %s",
            Result.Code, ErrorMessageOrDefault(Result.Message));
        return false;
    }

    Result = Number_Int64FromString(errorPool, u8"+1", NUMBER_BASE_AUTO_DETECT, &ParsedResult.Int64);
    if (Result.Code != ErrorCode_Success)
    {
        Test_FormatErrorMessage(errorMsg,
            u8"Error parsing integer with '+' sign at the start: %s",
            ErrorMessageOrDefault(Result.Message));
        return false;
    }

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