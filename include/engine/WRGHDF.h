#pragma once
#include "WRMemory.h"
#include "WRError.h"
#include "WRIO.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "WRCollection.h"


#define GHDF_ENTRY_ID_INVALID ((GHDFEntryID)0)


/* void* pointers in GHDF object setters are pointers to raw values to set,
* like int32_t*, unsigned char*, GHDFCompound*, etc. 
* GHDFObjectPool owns object pools for GHDF compounds, arrays and string buffers. They can be borrowed by users
* and returned for re-use afterwards. When returned, their datra is cleared, but allocated memory remains.
* When the pool is deconstructed, all objects owned by it are freed. */


// Types.
typedef uint64_t GHDFEntryID;

typedef struct GHDFCompoundStruct GHDFCompound;
typedef struct GHDFArrayStruct GHDFArray;
typedef struct GHDFObjectPoolStruct GHDFObjectPool;

typedef enum GHDFValueTypeEnum
{
    GHDFValueType_None = 0,
    GHDFValueType_UInt8 = 1,
    GHDFValueType_Int8 = 2,
    GHDFValueType_Int16 =3,
    GHDFValueType_UInt16 = 4,
    GHDFValueType_Int32 = 5,
    GHDFValueType_UInt32 = 6,
    GHDFValueType_Int64 = 7,
    GHDFValueType_UInt64 = 8,
    GHDFValueType_Float = 9,
    GHDFValueType_Double = 10,
    GHDFValueType_Boolean = 11,
    GHDFValueType_String = 12,
    GHDFValueType_Compound = 13,
    GHDFValueType_EncodedInteger = 14
} GHDFValueType;

typedef struct GHDFCompoundEntryTypeStruct
{
    GHDFValueType ValueType;
    bool IsArray;
} GHDFCompoundEntryType;

typedef struct GHDFObjectValueStruct
{
    GHDFValueType Type;
    union {
        uint8_t UInt8;
        int8_t Int8;
        uint16_t UInt16;
        int16_t Int16;
        uint32_t UInt32;
        int32_t Int32;
        uint64_t UInt64;
        int64_t Int64;
        float Float;
        double Double;
        bool Boolean;
        unsigned char* String;
        GHDFCompound* Compound;
        GHDFArray* Array;
        int64_t EncodedInteger;
    } Value;
} GHDFObjectValue;


// Functions.
Error GHDF_Write(const GHDFCompound* root, IOStream* stream);

Error GHDF_Read(IOStream* stream, GHDFObjectPool* objectPool, GHDFCompound** outRoot);


Error GHDFCompound_Construct1(GHDFCompound* self);

Error GHDFCompound_Deconstruct(GHDFCompound* self);

Error GHDFCompound_Clear(GHDFCompound* self);

Error GHDFCompound_Remove(GHDFCompound* self, GHDFEntryID id);

Error GHDFCompound_Set(GHDFCompound* self, GHDFEntryID id, GHDFValueType valueType, void* value);

Error GHDFCompound_Get(GHDFCompound* self, GHDFEntryID id, GHDFObjectValue* outEntry);

Error GHDFCompound_GetOptional(GHDFCompound* self, GHDFEntryID id, GHDFObjectValue* outEntry, bool* outWasFound);

Error GHDFCompound_GetVerified(GHDFCompound* self,
    GHDFEntryID id,
    GHDFCompoundEntryType expectedType,
    GHDFObjectValue* outEntry);

Error GHDFCompound_GetOptionalVerified(GHDFCompound* self,
    GHDFEntryID id,
    GHDFCompoundEntryType expectedType,
    GHDFObjectValue* outEntry,
    bool* outWasFound);

size_t GHDFCompound_GetEntryCount(GHDFCompound* self);

ICollection* GHDFCompound_AsEntryCollection(GHDFCompound* self);

ICollection* GHDFCompound_AsValueCollection(GHDFCompound* self);

ICollection* GHDFCompound_AsKeyCollection(GHDFCompound* self);


Error GHDFArray_Construct1(GHDFArray* self, GHDFValueType elementType);

Error GHDFArray_Deconstruct(GHDFArray* self);

Error GHDFArray_Clear(GHDFArray* self);

Error GHDFArray_RemoveAt(GHDFArray* self, size_t index);

Error GHDFArray_Add(GHDFArray* self, void* element);

Error GHDFArray_Insert(GHDFArray* self, size_t index, void* element);

Error GHDFArray_Replace(GHDFArray* self, size_t index, void* element);

Error GHDFArray_Get(GHDFArray* self, size_t index, GHDFObjectValue* outValue);

size_t GHDFArray_GetElementCount(GHDFArray* self);

GHDFValueType GHDFArray_GetElementType(GHDFArray* self);

ICollection* GHDFArray_AsElementCollection(GHDFArray* self);


Error GHDFObjectPool_Create(GHDFObjectPool** outPool);

Error GHDFObjectPool_Deconstruct(GHDFObjectPool* self);

Error GHDFObjectPool_BorrowCompound(GHDFObjectPool* self, GHDFCompound** outCompound);

Error GHDFObjectPool_BorrowArray(GHDFObjectPool* self, GHDFArray** outArray);

Error GHDFObjectPool_BorrowString(GHDFObjectPool* self, GenericBuffer** outStringBuffer);

Error GHDFObjectPool_ReturnCompound(GHDFObjectPool* self, GHDFCompound* compound, bool includeNestedStructures);

Error GHDFObjectPool_ReturnArray(GHDFObjectPool* self, GHDFArray* array, bool includeNestedStructures);

Error GHDFObjectPool_ReturnString(GHDFObjectPool* self, GenericBuffer* stringBuffer);


GHDFCompoundEntryType GHDF_CreateRegularType(GHDFValueType valueType);

GHDFCompoundEntryType GHDF_CreateArrayType(GHDFValueType valueType);