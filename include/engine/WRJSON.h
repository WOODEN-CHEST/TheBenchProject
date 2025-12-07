#pragma once
#include "WRError.h"
#include <stdint.h>
#include "WRMemory.h"


// Types.
typedef union JSONValueUnion
{
    const unsigned char* String;
    bool Boolean;
    int64_t Integer;
    double RealNumer;
} JSONValue;

typedef enum JSONTypeEnum
{
    JSONType_String,
    JSONType_Boolean,
    JSONType_Integer,
    JSONType_RealNumber,
    JSONType_Null,
    JSONType_Compound,
    JSONType_Array,
} JSONType;

typedef struct JSONEntryStruct
{
    JSONType _type;
    struct JSONEntryStruct* _sibling; // Next entry in an array or compound.
    JSONValue _value; // Actual value.
} JSONEntry;


// Functions.
Error JSON_Parse(ErrorMessagePool* errorPool, unsigned char* jsonStr, GenericBuffer* entryPool);

Error JSON_Parse(ErrorMessagePool* errorPool, unsigned char* jsonData, size_t jsonDataSize, GenericBuffer* entryPool);

Error JSON_Write(ErrorMessagePool* errorPool, GenericBuffer* dest, JSONEntry* entry, bool format, bool nullTerminate);