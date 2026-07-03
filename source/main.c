#include <stdio.h>
#include "wr/WRCompile.h"

// ==========================================================================================
// SAMPLE CODE START — lists the top-level entries of a directory. Safe to delete this whole
// block (the function below and its call in main) along with the WRFileSystem/WRError includes.
// ==========================================================================================
#include "wr/WRError.h"
#include "wr/WRFileSystem.h"

// Prints an error's message (or a placeholder) to stderr, then releases the error.
static void SamplePrintAndReleaseError(const char* context, Error* error)
{
    const unsigned char* message = error->Message;
    fprintf(stderr, "%s: %s\n", context, message ? (const char*)message : "(no message)");
    Error_Deconstruct(error);
}

// Lists every top-level entry of directoryPath to stdout, one name per line, or "No entries"
// if the directory is empty. Returns 0 on success, non-zero on failure (message on stderr).
static int SampleListDirectory(const char* directoryPath)
{
    DirectoryEntryEnumerator* enumerator = NULL;
    Error error = FileSystem_GetEntries((const unsigned char*)directoryPath,
        DirectorySearchOption_TopLevel, &enumerator);
    if (error.Code != ErrorCode_Success)
    {
        SamplePrintAndReleaseError("Failed to open directory", &error);
        return 1;
    }

    bool foundAny = false;
    while (true)
    {
        bool hasNext = false;
        error = DirectoryEntryEnumerator_HasNext(enumerator, &hasNext);
        if (error.Code != ErrorCode_Success)
        {
            SamplePrintAndReleaseError("Failed to read directory", &error);
            DirectoryEntryEnumerator_Deconstruct(enumerator);
            return 1;
        }
        if (!hasNext)
        {
            break;
        }

        FileSystemEntryInfo info = { 0 };
        error = DirectoryEntryEnumerator_Next(enumerator, &info);
        if (error.Code != ErrorCode_Success)
        {
            SamplePrintAndReleaseError("Failed to read directory entry", &error);
            DirectoryEntryEnumerator_Deconstruct(enumerator);
            return 1;
        }

        printf("%s\n", (const char*)info._name);
        foundAny = true;
        FileSystemEntryInfo_Deconstruct(&info);
    }

    if (!foundAny)
    {
        printf("No entries\n");
    }

    // FileSystem_GetEntries documents this deconstructor as always returning success, so its
    // result carries nothing to release.
    DirectoryEntryEnumerator_Deconstruct(enumerator);
    return 0;
}
// ==========================================================================================
// SAMPLE CODE END
// ==========================================================================================

int main(int argc, char** argv)
{
    // SAMPLE CODE — pass a directory path to list its top-level entries. Remove this `if`
    // block (and SampleListDirectory above) to restore the default behavior below.
    if (argc >= 2)
    {
        return SampleListDirectory(argv[1]);
    }
    // END SAMPLE CODE.

    UNUSED(argc);
    UNUSED(argv);
    printf("Hello World!\n");
}
