#pragma once
#include "WRError.h"
#include <stdint.h>
#include "WRMemory.h"
#include "WRError.h"
#include "WRCollection.h"


/**
 * This module provides JSON serialization and deserialization stuff.
 * 
 * All JSON strings, compounds and arrays must be acquired from a JSON object pool, 
 * then returned to the pool when no longer used.
 * 
 * Compound string keys are copied into pool-managed storage when inserted.
 * 
 * String values are stored as generic buffers borrowed from the pool.
 * 
 * The compound and array get functions work with the following mannet:
 * - The regular get functions return an error if no value is found, otherwise return the value.
 * - Optional modifier doesnt return an error for no value found, it also returns whether the value was found.
 * - The verified modifier, if a value is present, additionally returns an error if the value is not of the expected type.
 * Both optional and verified modifiers can be present at the same time.
 */



// Types.
typedef struct JSONCompoundStruct JSONCompound;
typedef struct JSONArrayStruct JSONArray;
typedef struct JSONObjectPoolStruct JSONObjectPool;

typedef enum JSONValueTypeEnum
{
    JSONValueType_None = 0,
    JSONValueType_String = 1,
    JSONValueType_Boolean = 2,
    JSONValueType_Integer = 3,
    JSONValueType_RealNumber = 4,
    JSONValueType_Null = 5,
    JSONValueType_Compound = 6,
    JSONValueType_Array = 7,
} JSONValueType; 

typedef struct JSONObjectValueStruct
{
    JSONValueType Type;
    union 
    {
        int64_t Integer;
        double RealNumber;
        bool Boolean;
        GenericBuffer* String;
        JSONCompound* Compound;
        JSONArray* Array;
        void* Null;
    } Value;
} JSONObjectValue;

typedef enum JSONSerializeFlagsEnum
{
    JSONSerializeFlags_None = 0,
    JSONSerializeFlags_NullTerminated = (1 << 0),
    JSONSerializeFlags_PrettyPrinted = (1 << 1),
} JSONSerializeFlags;

typedef struct JSONCompoundEntryStruct
{
    const unsigned char* Key;
    JSONObjectValue Value;
} JSONCompoundEntry;

typedef struct JSONArrayIndexedValueStruct
{
    size_t Index;
    JSONObjectValue Value;
} JSONArrayIndexedValue;


// Functions.
static inline JSONObjectValue JSONObjectValue_CreateString(GenericBuffer* value)
{
    return (JSONObjectValue){ .Type = JSONValueType_String, .Value.String = value };
}

static inline JSONObjectValue JSONObjectValue_CreateBoolean(bool value)
{
    return (JSONObjectValue){ .Type = JSONValueType_Boolean, .Value.Boolean = value };
}

static inline JSONObjectValue JSONObjectValue_CreateInteger(int64_t value)
{
    return (JSONObjectValue){ .Type = JSONValueType_Integer, .Value.Integer = value };
}

static inline JSONObjectValue JSONObjectValue_CreateRealNumber(double value)
{
    return (JSONObjectValue){ .Type = JSONValueType_RealNumber, .Value.RealNumber = value };
}

static inline JSONObjectValue JSONObjectValue_CreateNull()
{
    return (JSONObjectValue){ .Type = JSONValueType_Null, .Value.Null = NULL };
}

static inline JSONObjectValue JSONObjectValue_CreateCompound(JSONCompound* value)
{
    return (JSONObjectValue){ .Type = JSONValueType_Compound, .Value.Compound = value };
}

static inline JSONObjectValue JSONObjectValue_CreateArray(JSONArray* value)
{
    return (JSONObjectValue){ .Type = JSONValueType_Array, .Value.Array = value };
}


Error JSON_Serialize(JSONObjectValue* value, GenericBuffer* destination, JSONSerializeFlags flags);

Error JSON_Deserialize(JSONObjectPool* pool, GenericBuffer* source, JSONObjectValue* outValue);


Error JSONCompound_Set(JSONCompound* self, const unsigned char* key, JSONObjectValue* value);

Error JSONCompound_Get(JSONCompound* self, const unsigned char* key, JSONObjectValue* outValue);

Error JSONCompound_GetOptional(JSONCompound* self,
    const unsigned char* key, 
    JSONObjectValue* outValue,
    bool* outWasFound);

Error JSONCompound_GetVerified(JSONCompound* self, 
    const unsigned char* key,
    JSONValueType expectedType,
    JSONObjectValue* outValue);

Error JSONCompound_GetOptionalVerified(JSONCompound* self,
    const unsigned char* key,
    JSONValueType expectedType, 
    JSONObjectValue* outValue, 
    bool* outWasFound);

Error JSONCompound_Clear(JSONCompound* self);

Error JSONCompound_Remove(JSONCompound* self, const unsigned char* key);

size_t JSONCompound_GetEntryCount(JSONCompound* self);

/* Iterates over JSONCompoundEntry values. */
ICollection* JSONCompound_GetEntryCollection(JSONCompound* self);

/* Iterates over JSONObjectValue values. */
ICollection* JSONCompound_GetValueCollection(JSONCompound* self);

/* Iterates over string key values (unsigned char*). */
ICollection* JSONCompound_GetKeyCollection(JSONCompound* self);



Error JSONArray_Replace(JSONArray* self, size_t index, JSONObjectValue* value);

Error JSONArray_Add(JSONArray* self, JSONObjectValue* value);

Error JSONArray_Insert(JSONArray* self, size_t index, JSONObjectValue* value);

Error JSONArray_Get(JSONArray* self, size_t index, JSONObjectValue* outValue);

Error JSONArray_GetOptional(JSONArray* self, size_t index, JSONObjectValue* outValue, bool* outWasFound);

Error JSONArray_GetVerified(JSONArray* self, size_t index, JSONValueType expectedType, JSONObjectValue* outValue);

Error JSONArray_GetOptionalVerified(JSONArray* self, 
    size_t index,
    JSONValueType expectedType,
    JSONObjectValue* outValue,
    bool* outWasFound);

Error JSONArray_Clear(JSONArray* self);

Error JSONArray_RemoveAt(JSONArray* self, size_t index);

size_t JSONArray_GetElementCount(JSONArray* self);

/* Iterates over JSONArrayIndexedValue values. */
ICollection* JSONArray_GetElementCollection(JSONArray* self);


Error JSONObjectPool_Create(JSONObjectPool** outPool);

Error JSONObjectPool_Deconstruct(JSONObjectPool* self);

Error JSONObjectPool_BorrowCompound(JSONObjectPool* self, JSONCompound** outCompound);

Error JSONObjectPool_BorrowArray(JSONObjectPool* self, JSONArray** outArray);

Error JSONObjectPool_BorrowString(JSONObjectPool* self, GenericBuffer** outStringBuffer);

Error JSONObjectPool_ReturnCompound(JSONObjectPool* self, JSONCompound* compound, bool includeNestedStructures);

Error JSONObjectPool_ReturnArray(JSONObjectPool* self, JSONArray* array, bool includeNestedStructures);

Error JSONObjectPool_ReturnString(JSONObjectPool* self, GenericBuffer* stringBuffer);

Error JSONObjectPool_ReturnValue(JSONObjectPool* self, JSONObjectValue* value);
