#include <stdio.h>
#include "WRError.h"
#include "WRUnicode.h"
#include "WRUnicodeLoader.h"

// Functions.
int main()
{
    ErrorMessagePool ErrorPool;
    ErrorMessagePool_Construct1(&ErrorPool);

    UnicodeData Unicode;
    Error Result = UnicodeData_Load(&ErrorPool,
        u8"/home/wooden_chest/Workstations/Projects/TheBenchProject/compile/out/asset/text/unicode_data.txt",
        &Unicode);
        
    if (Result.Code != ErrorCode_Success)
    {
        printf("Error: %s\n", Result.Message);
        ErrorMessagePool_Clear(&ErrorPool);
    }
    else
    {
        printf("Unicode data loaded!\n");
    }

    ErrorMessagePool_Deconstruct1(&ErrorPool);
    return 0;
}