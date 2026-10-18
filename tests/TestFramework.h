#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "wr/WRError.h"
#include "wr/WRMemory.h"


/**
 * The project's minimal test framework: plain C, no dependencies beyond the game's own libraries.
 *
 * A suite is a static array of TestCase entries plus a public "<Suite>Tests_Run(TestRun*)" function, declared
 * in TestSuites.h and called from TestMain.c, that hands the array to TestRun_RunSuite. A test case is a
 * function taking the TestRun; it records checks with the TEST_CHECK* macros, which print the file, line and
 * expression of every failed check and let the test continue.
 *
 * Rules for test cases:
 *  - Free everything you allocate. TestRun_RunSuite compares WRMemory's live allocation count before and after
 *    every case and reports any difference as a leak (a failure).
 *  - Run headless: never call anything that needs a window or a GL context (loading textures, fonts, models or
 *    shaders does). Test the logic around them instead.
 *  - Write files only under your own work directory (TestRun_CreateWorkDirectory); run_linux.sh deletes the
 *    whole work tree before every run.
 *
 * Test names and failed expressions are plain `char` strings because they come from the preprocessor's
 * stringification; everything passed to game or WRFramework code is UTF-8 `unsigned char` as usual. Tests are
 * tooling, not game code, so they print their results to stdout directly.
 */


// Types.
/**
 * @brief State and results of one test run.
 *
 * Construct with TestRun_Construct, run suites with TestRun_RunSuite, print the summary with TestRun_Finish and
 * release with TestRun_Deconstruct. Fields are read-only outside TestFramework.c.
 */
typedef struct TestRunStruct
{
    /** @brief Directory containing the test executable (owned, NUL-terminated); work trees go under it. */
    GenericBuffer _executableDirectory;
    /** @brief Name of the suite currently running (borrowed string literal), or NULL. */
    const char* _suiteName;
    /** @brief Name of the test case currently running (borrowed string literal), or NULL. */
    const char* _testName;
    /** @brief Whether a check in the current test case has failed. */
    bool _hasCurrentTestFailed;
    /** @brief Number of test cases run. */
    size_t _testCount;
    /** @brief Number of test cases with at least one failed check or a leak. */
    size_t _failedTestCount;
    /** @brief Number of checks evaluated. */
    size_t _checkCount;
    /** @brief Number of checks that failed. */
    size_t _failedCheckCount;
} TestRun;

/**
 * @brief A test case body.
 * @param run The test run; record checks against it with the TEST_CHECK* macros.
 */
typedef void (*TestFunction)(TestRun* run);

/** @brief One named test case of a suite. */
typedef struct TestCaseStruct
{
    /** @brief Name printed in the results (a string literal). */
    const char* Name;
    /** @brief The test body. */
    TestFunction Function;
} TestCase;


// Functions.
/**
 * @brief Initializes a test run and finds the executable's directory (the base for work directories).
 * @param self [out] The run to initialize. Must not be NULL.
 * @returns Success, or the error from FileSystem_GetExecutableDirectory.
 */
Error TestRun_Construct(TestRun* self);

/**
 * @brief Releases a test run's memory.
 * @param self The run; may be NULL.
 */
void TestRun_Deconstruct(TestRun* self);

/**
 * @brief Runs every case of a suite, printing one result line per case and reporting leaked allocations.
 * @param self The run. Must not be NULL.
 * @param suiteName Suite name for the output and for work directory names (a string literal).
 * @param cases The cases to run, in order. Must not be NULL unless @p caseCount is 0.
 * @param caseCount Number of entries in @p cases.
 */
void TestRun_RunSuite(TestRun* self, const char* suiteName, const TestCase* cases, size_t caseCount);

/**
 * @brief Prints the summary of the run.
 * @param self The run. Must not be NULL.
 * @returns The process exit code: 0 if every check passed and nothing leaked, 1 otherwise.
 */
int TestRun_Finish(TestRun* self);

/**
 * @brief Records a boolean check (use TEST_CHECK).
 * @returns @p condition, so a test can stop early when a precondition fails.
 */
bool TestRun_Check(TestRun* self, bool condition, const char* expression, const char* file, int line);

/**
 * @brief Records that @p result is success, printing its message otherwise; always deconstructs @p result
 *        (use TEST_CHECK_SUCCESS).
 * @returns true if @p result was success.
 */
bool TestRun_CheckSuccess(TestRun* self, Error result, const char* expression, const char* file, int line);

/**
 * @brief Records that @p result carries @p expected; always deconstructs @p result (use TEST_CHECK_ERROR).
 * @returns true if the codes match.
 */
bool TestRun_CheckErrorCode(TestRun* self, Error result, ErrorCode expected, const char* expression,
    const char* file, int line);

/**
 * @brief Creates "<executable directory>/work/<suite>/<name>" (and its parents) and appends its path,
 *        NUL-terminated, to @p outPath.
 * @param self The run; the current suite name is used. Must not be NULL.
 * @param name Directory name for the calling test (a string literal; normally the test's own name).
 * @param outPath [out] Byte buffer the path is appended to. Must not be NULL.
 * @returns Success, or a path/filesystem error.
 */
Error TestRun_CreateWorkDirectory(TestRun* self, const char* name, GenericBuffer* outPath);

/**
 * @brief Appends "<repository root>/<relativePath>", NUL-terminated, to @p outPath (the test binary runs from
 *        tests/out, two levels below the root). For tests that check files the game ships, such as compile/out/asset.
 * @param self The run. Must not be NULL.
 * @param relativePath Path from the repository root using '/' separators (a string literal). Must not be NULL.
 * @param outPath [out] Byte buffer the path is appended to. Must not be NULL.
 * @returns Success, or a formatting error.
 */
Error TestRun_BuildRepositoryPath(TestRun* self, const char* relativePath, GenericBuffer* outPath);

/**
 * @brief Writes @p text to "<directory>/<relativePath>", creating missing parent directories.
 * @param directory Absolute UTF-8 directory (typically a work directory). Must not be NULL.
 * @param relativePath Relative file path using '/' separators (a string literal). Must not be NULL.
 * @param text File contents. Must not be NULL.
 * @returns Success, or a path/filesystem error.
 */
Error Test_WriteTextFile(const unsigned char* directory, const char* relativePath, const char* text);

/**
 * @brief Counts the regular files directly inside @p directory (not recursive).
 * @param directory The directory to inspect. Must not be NULL.
 * @param outCount [out] Receives the count; 0 on failure. Must not be NULL.
 * @returns Success, or the enumeration error.
 */
Error Test_CountFiles(const unsigned char* directory, size_t* outCount);


// Check macros.
/** @brief Checks a boolean condition. */
#define TEST_CHECK(run, condition) \
    TestRun_Check((run), (condition), #condition, __FILE__, __LINE__)
/** @brief Checks that an Error-returning expression succeeds (the Error is consumed). */
#define TEST_CHECK_SUCCESS(run, errorExpression) \
    TestRun_CheckSuccess((run), (errorExpression), #errorExpression, __FILE__, __LINE__)
/** @brief Checks that an Error-returning expression fails with @p expectedCode (the Error is consumed). */
#define TEST_CHECK_ERROR(run, errorExpression, expectedCode) \
    TestRun_CheckErrorCode((run), (errorExpression), (expectedCode), #errorExpression, __FILE__, __LINE__)
