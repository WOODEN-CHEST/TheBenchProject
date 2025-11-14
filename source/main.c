#include <stdio.h>
#include "WRError.h"
#include "WRUnicode.h"
#include "WRUnicodeLoader.h"
#include "WRChar.h"
#include "WRNumber.h"
#include "time.h"


// Functions.
int main()
{
    ErrorMessagePool ErrorPool;
    ErrorMessagePool_Construct1(&ErrorPool);

    ErrorMessagePool_Deconstruct1(&ErrorPool);


    return 0;
}