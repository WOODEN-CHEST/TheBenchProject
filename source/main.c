#include <stdio.h>
#include "WRError.h"
#include "WRUnicode.h"
#include "WRUnicodeLoader.h"
#include "WRChar.h"
#include "WRNumber.h"

// Functions.
int main()
{
    ErrorMessagePool Pool;
    ErrorMessagePool_Construct1(&Pool);

    unsigned char ConstBuffer[128];
    GenericBuffer Buffer = GenericBuffer_CreateConstant(ConstBuffer, sizeof(ConstBuffer));


    Error Result = Number_Int32ToString(&Pool, 15, 17, Buffer);
    if (Result.Code != ErrorCode_Success)
    {
        printf("%s\n", Result.Message);
        ErrorMessagePool_Clear(&Pool);
    }
    else
    {
        printf("%s\n", (char*)ConstBuffer);
    }

    ErrorMessagePool_Deconstruct1(&Pool);

    return 0;
}