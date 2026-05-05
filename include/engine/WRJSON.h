#pragma once
#include "WRError.h"
#include <stdint.h>
#include "WRMemory.h"
#include "WRError.h"


// Types.
typedef struct JSONCompoundStruct JSONCompound;
typedef struct JSONCArrayStruct JSONCArray;
typedef struct JSONObjectPoolStruct JSONObjectPool;



// Functions.
Error JSONObjectPool_Create(JSONObjectPool** outPool);

Error JSONObjectPool_Deconstruct(JSONObjectPool* self);

Error JSONObjectPool_BorrowCompound(JSONObjectPool* self, JSONCompoundStruct** outCompound);

Error JSONObjectPool_BorrowArray(JSONObjectPool* self, JSONCompoundStruct elementType, JSONCArray** outArray);

Error JSONObjectPool_BorrowString(JSONObjectPool* self, GenericBuffer** outStringBuffer);

Error JSONObjectPool_ReturnCompound(JSONObjectPool* self, JSONCompoundStruct* compound, bool includeNestedStructures);

Error JSONObjectPool_ReturnArray(JSONObjectPool* self, JSONCompoundStruct* array, bool includeNestedStructures);

Error JSONObjectPool_ReturnString(JSONObjectPool* self, GenericBuffer* stringBuffer);