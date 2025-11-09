#pragma once
#include "WRUnicode.h"


// Functions.
Error UnicodeData_Load(ErrorMessagePool* errorPool, const unsigned char* dataBaseFilePath, UnicodeData* data);

void UnicodeData_Deconstruct(UnicodeData* data);