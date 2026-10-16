#include "TestSuites.h"
#include "Logger.h"
#include "wr/WRFileSystem.h"
#include "wr/WRPath.h"
#include <stdio.h>
#include <string.h>


// Macros.
/** Initial capacity (bytes) of path and content buffers; they grow as needed. */
#define LOGGER_TEST_BUFFER_CAPACITY ((size_t)256)
/** Archived logs pre-created by the pruning test; more than the logger keeps. */
#define LOGGER_TEST_EXTRA_ARCHIVES ((size_t)35)
/** Archived logs the logger keeps after pruning (LOGGER_MAX_ARCHIVED_LOGS in Logger.c). */
#define LOGGER_TEST_KEPT_ARCHIVES ((size_t)30)


// Static functions.
/* Reads "<directory>/<relativePath>" into @p outText (NUL-terminated). */
static Error ReadTextFile(const unsigned char* directory, const char* relativePath, GenericBuffer* outText)
{
    GenericBuffer FilePath;
    GenericBuffer_AllocateVariable(&FilePath, LOGGER_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    Error Result = Path_Append(directory, (const unsigned char*)relativePath, &FilePath);
    if (Result.Code == ErrorCode_Success)
    {
        Result = FileSystem_ReadAllBytes(FilePath._data, outText);
    }
    if ((Result.Code == ErrorCode_Success) && !GenericBuffer_NullTerminate(outText))
    {
        Result = Error_Construct3(ErrorCode_BufferTooSmall, (const unsigned char*)u8"Could not terminate the text.");
    }
    Memory_Free(FilePath._data);
    return Result;
}

/* Appends "<directory>/logs/archived" to @p outPath. */
static Error BuildArchivePath(const unsigned char* directory, GenericBuffer* outPath)
{
    return Path_Append(directory, (const unsigned char*)u8"logs/archived", outPath);
}

static void ConstructRotatesIntoBaseDirectory(TestRun* run)
{
    GenericBuffer Base;
    GenericBuffer_AllocateVariable(&Base, LOGGER_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    GenericBuffer Archive;
    GenericBuffer_AllocateVariable(&Archive, LOGGER_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, LOGGER_TEST_BUFFER_CAPACITY, sizeof(unsigned char));

    if (TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "rotates_into_base", &Base))
        && TEST_CHECK_SUCCESS(run, Test_WriteTextFile(Base._data, "logs/latest.log", "previous session\n"))
        && TEST_CHECK_SUCCESS(run, BuildArchivePath(Base._data, &Archive)))
    {
        Logger Log;
        if (TEST_CHECK_SUCCESS(run, Logger_Construct(&Log, Base._data)))
        {
            TEST_CHECK_SUCCESS(run, Logger_LogInfoFormatted(&Log, (const unsigned char*)u8"value %d", 42));
            TEST_CHECK_SUCCESS(run, Logger_Deconstruct(&Log));
        }

        size_t ArchiveCount = 0;
        TEST_CHECK_SUCCESS(run, Test_CountFiles(Archive._data, &ArchiveCount));
        TEST_CHECK(run, ArchiveCount == 1);
        if (TEST_CHECK_SUCCESS(run, ReadTextFile(Base._data, "logs/latest.log", &Text)))
        {
            TEST_CHECK(run, strstr((const char*)Text._data, "previous session") == NULL);
            TEST_CHECK(run, strstr((const char*)Text._data, "value 42") != NULL);
        }
    }

    Memory_Free(Text._data);
    Memory_Free(Archive._data);
    Memory_Free(Base._data);
}

static void ConstructPrunesToTheNewestThirty(TestRun* run)
{
    GenericBuffer Base;
    GenericBuffer_AllocateVariable(&Base, LOGGER_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    GenericBuffer Archive;
    GenericBuffer_AllocateVariable(&Archive, LOGGER_TEST_BUFFER_CAPACITY, sizeof(unsigned char));

    bool IsReady = TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "prunes_to_thirty", &Base))
        && TEST_CHECK_SUCCESS(run, BuildArchivePath(Base._data, &Archive));
    for (size_t Index = 0; IsReady && (Index < LOGGER_TEST_EXTRA_ARCHIVES); Index++)
    {
        char RelativePath[64];
        snprintf(RelativePath, sizeof(RelativePath), "logs/archived/old_%02d.log", (int)Index);
        IsReady = TEST_CHECK_SUCCESS(run, Test_WriteTextFile(Base._data, RelativePath, "old\n"));
    }

    if (IsReady)
    {
        Logger Log;
        if (TEST_CHECK_SUCCESS(run, Logger_Construct(&Log, Base._data)))
        {
            TEST_CHECK_SUCCESS(run, Logger_Deconstruct(&Log));
        }
        size_t ArchiveCount = 0;
        TEST_CHECK_SUCCESS(run, Test_CountFiles(Archive._data, &ArchiveCount));
        TEST_CHECK(run, ArchiveCount == LOGGER_TEST_KEPT_ARCHIVES);
    }

    Memory_Free(Archive._data);
    Memory_Free(Base._data);
}

static void ArchivingWithoutALogIsANoOp(TestRun* run)
{
    GenericBuffer Base;
    GenericBuffer_AllocateVariable(&Base, LOGGER_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    if (TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "archive_no_log", &Base)))
    {
        TEST_CHECK_SUCCESS(run, Logger_ArchiveLatestLog(Base._data));
    }
    Memory_Free(Base._data);
}


// Public functions.
void LoggerTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "construct_rotates_into_base_directory", ConstructRotatesIntoBaseDirectory },
        { "construct_prunes_to_the_newest_thirty", ConstructPrunesToTheNewestThirty },
        { "archiving_without_a_log_is_a_no_op", ArchivingWithoutALogIsANoOp },
    };
    TestRun_RunSuite(run, "Logger", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
