#pragma once
#include "WRIO.h"
#include "WRUnicode.h"


// Functions.
Error UnicodeData_Load(const unsigned char* dataBaseFilePath, UnicodeData* data);

Error UnicodeData_LoadFromStream(IOStream* stream, UnicodeData* data);

Error UnicodeData_CreateEmpty(UnicodeData* data);

void UnicodeData_Deconstruct(UnicodeData* data);
