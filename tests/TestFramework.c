#include "TestFramework.h"
#include "wr/WRFileSystem.h"
#include "wr/WRPath.h"
#include "wr/WRString.h"
#include <stdio.h>
#include <inttypes.h>


// Macros.
/** Initial capacity (bytes) of path buffers; they grow as needed. */
#define TEST_PATH_INITIAL_CAPACITY ((size_t)256)
/** Name of the directory (next to the test executable) that holds every test's files. */
#define TEST_WORK_DIRECTORY_NAME ((const unsigned char*)u8"work")


// Static functions.
static const char* GetErrorText(const Error* error)
{
    return (error->Message != NULL) ? (const char*)error->Message : "(no message)";
}

static void RecordFailure(TestRun* self, const char* file, int line, const char* description, const char* detail)
{
    if (!self->_hasCurrentTestFailed)
    {
        printf("  FAIL  %s\n", (self->_testName != NULL) ? self->_testName : "(outside a test)");
    }
    self->_hasCurrentTestFailed = true;
    self->_failedCheckCount++;
    printf("        %s:%d: %s", file, line, description);
    if (detail != NULL)
    {
        printf(" (%s)", detail);
    }
    printf("\n");
}


// Public functions.
Error TestRun_Construct(TestRun* self)
{
    Memory_Zero(self, sizeof(*self));
    GenericBuffer_AllocateVariable(&self->_executableDirectory, TEST_PATH_INITIAL_CAPACITY, sizeof(unsigned char));
    return FileSystem_GetExecutableDirectory(&self->_executableDirectory);
}

void TestRun_Deconstruct(TestRun* self)
{
    if (self == NULL)
    {
        return;
    }
    Memory_Free(self->_executableDirectory._data);
    Memory_Zero(self, sizeof(*self));
}

void TestRun_RunSuite(TestRun* self, const char* suiteName, const TestCase* cases, size_t caseCount)
{
    self->_suiteName = suiteName;
    printf("[%s]\n", suiteName);
    for (size_t Index = 0; Index < caseCount; Index++)
    {
        self->_testName = cases[Index].Name;
        self->_hasCurrentTestFailed = false;
        self->_testCount++;

        size_t AllocationsBefore = Memory_GetCurrentAllocationCount();
        cases[Index].Function(self);
        size_t AllocationsAfter = Memory_GetCurrentAllocationCount();
        self->_checkCount++; // The leak check every case gets.
        if (AllocationsAfter != AllocationsBefore)
        {
            char Detail[64];
            snprintf(Detail, sizeof(Detail), "live allocations %" PRIu64 " -> %" PRIu64,
                (uint64_t)AllocationsBefore, (uint64_t)AllocationsAfter);
            RecordFailure(self, __FILE__, __LINE__, "the test leaked memory", Detail);
        }

        if (self->_hasCurrentTestFailed)
        {
            self->_failedTestCount++;
        }
        else
        {
            printf("  ok    %s\n", cases[Index].Name);
        }
    }
    self->_testName = NULL;
    self->_suiteName = NULL;
}

int TestRun_Finish(TestRun* self)
{
    printf("\n%" PRIu64 " tests, %" PRIu64 " failed (%" PRIu64 " checks, %" PRIu64 " failed)\n",
        (uint64_t)self->_testCount, (uint64_t)self->_failedTestCount,
        (uint64_t)self->_checkCount, (uint64_t)self->_failedCheckCount);
    return (self->_failedTestCount == 0) ? 0 : 1;
}

bool TestRun_Check(TestRun* self, bool condition, const char* expression, const char* file, int line)
{
    self->_checkCount++;
    if (!condition)
    {
        RecordFailure(self, file, line, expression, NULL);
    }
    return condition;
}

bool TestRun_CheckSuccess(TestRun* self, Error result, const char* expression, const char* file, int line)
{
    self->_checkCount++;
    bool IsSuccess = (result.Code == ErrorCode_Success);
    if (!IsSuccess)
    {
        RecordFailure(self, file, line, expression, GetErrorText(&result));
    }
    Error_Deconstruct(&result);
    return IsSuccess;
}

bool TestRun_CheckErrorCode(TestRun* self, Error result, ErrorCode expected, const char* expression,
    const char* file, int line)
{
    self->_checkCount++;
    bool IsMatch = (result.Code == expected);
    if (!IsMatch)
    {
        char Detail[256];
        snprintf(Detail, sizeof(Detail), "expected error code %d, got %d: %s", (int)expected, (int)result.Code,
            GetErrorText(&result));
        RecordFailure(self, file, line, expression, Detail);
    }
    Error_Deconstruct(&result);
    return IsMatch;
}

Error TestRun_CreateWorkDirectory(TestRun* self, const char* name, GenericBuffer* outPath)
{
    const unsigned char* Parts[4] =
    {
        self->_executableDirectory._data,
        TEST_WORK_DIRECTORY_NAME,
        (const unsigned char*)self->_suiteName,
        (const unsigned char*)name
    };
    Error CombineResult = Path_Combine(Parts, 4U, outPath);
    if (CombineResult.Code != ErrorCode_Success)
    {
        return CombineResult;
    }
    return FileSystem_CreateAllDirectories(outPath->_data);
}

Error TestRun_BuildRepositoryPath(TestRun* self, const char* relativePath, GenericBuffer* outPath)
{
    return StringUTF8_Format((const unsigned char*)u8"%s/../../%s", outPath, (const char*)self->_executableDirectory._data,
        relativePath);
}

Error Test_WriteTextFile(const unsigned char* directory, const char* relativePath, const char* text)
{
    GenericBuffer FilePath;
    GenericBuffer_AllocateVariable(&FilePath, TEST_PATH_INITIAL_CAPACITY, sizeof(unsigned char));
    Error Result = Path_Append(directory, (const unsigned char*)relativePath, &FilePath);

    GenericBuffer ParentPath;
    GenericBuffer_AllocateVariable(&ParentPath, TEST_PATH_INITIAL_CAPACITY, sizeof(unsigned char));
    if (Result.Code == ErrorCode_Success)
    {
        Result = Path_GetParentPath(FilePath._data, &ParentPath);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = FileSystem_CreateAllDirectories(ParentPath._data);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = FileSystem_WriteAllText(FilePath._data, (const unsigned char*)text);
    }

    Memory_Free(ParentPath._data);
    Memory_Free(FilePath._data);
    return Result;
}

Error Test_CountFiles(const unsigned char* directory, size_t* outCount)
{
    *outCount = 0;
    DirectoryEntryEnumerator* Enumerator = NULL;
    Error Result = FileSystem_GetFiles(directory, DirectorySearchOption_TopLevel, &Enumerator);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    size_t Count = 0;
    while (true)
    {
        bool HasNext = false;
        Result = DirectoryEntryEnumerator_HasNext(Enumerator, &HasNext);
        if ((Result.Code != ErrorCode_Success) || !HasNext)
        {
            break;
        }
        FileSystemEntryInfo Entry;
        Result = DirectoryEntryEnumerator_Next(Enumerator, &Entry);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }
        FileSystemEntryInfo_Deconstruct(&Entry);
        Count++;
    }

    Error CloseResult = DirectoryEntryEnumerator_Deconstruct(Enumerator);
    Error_Deconstruct(&CloseResult);
    if (Result.Code == ErrorCode_Success)
    {
        *outCount = Count;
    }
    return Result;
}
