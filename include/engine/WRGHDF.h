#pragma once
#include "WRCollection.h"
#include "WRError.h"
#include "WRIO.h"
#include "WRMemory.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


#define GHDF_ENTRY_ID_INVALID ((GHDFEntryID)0)


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
    GHDFValueType_Int16 = 3,
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
    union
    {
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
        GenericBuffer* String;
        GHDFCompound* Compound;
        GHDFArray* Array;
        int64_t EncodedInteger;
    } Value;
} GHDFObjectValue;

typedef struct GHDFCompoundEntryStruct
{
    GHDFEntryID Id;
    GHDFCompoundEntryType EntryType;
    GHDFObjectValue Value;
} GHDFCompoundEntry;

typedef struct GHDFArrayIndexedValueStruct
{
    size_t Index;
    GHDFObjectValue Value;
} GHDFArrayIndexedValue;


// Functions.

static inline GHDFObjectValue GHDFObjectValue_CreateUInt8(uint8_t value)
{
    return (GHDFObjectValue){ .Type = GHDFValueType_UInt8, .Value.UInt8 = value };
}

static inline GHDFObjectValue GHDFObjectValue_CreateInt8(int8_t value)
{
    return (GHDFObjectValue){ .Type = GHDFValueType_Int8, .Value.Int8 = value };
}

static inline GHDFObjectValue GHDFObjectValue_CreateInt16(int16_t value)
{
    return (GHDFObjectValue){ .Type = GHDFValueType_Int16, .Value.Int16 = value };
}

static inline GHDFObjectValue GHDFObjectValue_CreateUInt16(uint16_t value)
{
    return (GHDFObjectValue){ .Type = GHDFValueType_UInt16, .Value.UInt16 = value };
}

static inline GHDFObjectValue GHDFObjectValue_CreateInt32(int32_t value)
{
    return (GHDFObjectValue){ .Type = GHDFValueType_Int32, .Value.Int32 = value };
}

static inline GHDFObjectValue GHDFObjectValue_CreateUInt32(uint32_t value)
{
    return (GHDFObjectValue){ .Type = GHDFValueType_UInt32, .Value.UInt32 = value };
}

static inline GHDFObjectValue GHDFObjectValue_CreateInt64(int64_t value)
{
    return (GHDFObjectValue){ .Type = GHDFValueType_Int64, .Value.Int64 = value };
}

static inline GHDFObjectValue GHDFObjectValue_CreateUInt64(uint64_t value)
{
    return (GHDFObjectValue){ .Type = GHDFValueType_UInt64, .Value.UInt64 = value };
}

static inline GHDFObjectValue GHDFObjectValue_CreateFloat(float value)
{
    return (GHDFObjectValue){ .Type = GHDFValueType_Float, .Value.Float = value };
}

static inline GHDFObjectValue GHDFObjectValue_CreateDouble(double value)
{
    return (GHDFObjectValue){ .Type = GHDFValueType_Double, .Value.Double = value };
}

static inline GHDFObjectValue GHDFObjectValue_CreateBoolean(bool value)
{
    return (GHDFObjectValue){ .Type = GHDFValueType_Boolean, .Value.Boolean = value };
}

static inline GHDFObjectValue GHDFObjectValue_CreateString(GenericBuffer* value)
{
    return (GHDFObjectValue){ .Type = GHDFValueType_String, .Value.String = value };
}

static inline GHDFObjectValue GHDFObjectValue_CreateCompound(GHDFCompound* value)
{
    return (GHDFObjectValue){ .Type = GHDFValueType_Compound, .Value.Compound = value };
}

static inline GHDFObjectValue GHDFObjectValue_CreateArray(GHDFArray* value, GHDFValueType valueType)
{
    return (GHDFObjectValue){ .Type = valueType, .Value.Array = value };
}

static inline GHDFObjectValue GHDFObjectValue_CreateEncodedInteger(int64_t value)
{
    return (GHDFObjectValue){ .Type = GHDFValueType_EncodedInteger, .Value.EncodedInteger = value };
}

Error GHDF_Write(const GHDFCompound* root, IOStream* stream);

Error GHDF_Read(IOStream* stream, GHDFObjectPool* objectPool, GHDFCompound** outRoot);

Error GHDFCompound_Clear(GHDFCompound* self);

Error GHDFCompound_Remove(GHDFCompound* self, GHDFEntryID id);

Error GHDFCompound_SetValue(GHDFCompound* self,
    GHDFEntryID id,
    GHDFCompoundEntryType entryType,
    const GHDFObjectValue* value);

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

/* Enumerates GHDFCompoundEntry values. */
ICollection* GHDFCompound_GetEntryCollection(GHDFCompound* self);

/* Enumerates GHDFObjectValue values. */
ICollection* GHDFCompound_GetValueCollection(GHDFCompound* self);

/* Enumerates GHDFEntryID values. */
ICollection* GHDFCompound_GetKeyCollection(GHDFCompound* self);

Error GHDFArray_Clear(GHDFArray* self);

Error GHDFArray_RemoveAt(GHDFArray* self, size_t index);

Error GHDFArray_AddValue(GHDFArray* self, const GHDFObjectValue* value);

Error GHDFArray_InsertValue(GHDFArray* self, size_t index, const GHDFObjectValue* value);

Error GHDFArray_ReplaceValue(GHDFArray* self, size_t index, const GHDFObjectValue* value);

Error GHDFArray_Get(GHDFArray* self, size_t index, GHDFObjectValue* outValue);

/* Appends in-memory array elements to destination without per-element conversion.
 * Destination must use the same element layout as this GHDF array's in-memory storage.
 * Example: a UInt8 GHDF array appends bytes, an Int16 GHDF array appends int16_t values. */
Error GHDFArray_CopyRawBytes(GHDFArray* self,
    size_t startIndex,
    size_t elementCount,
    GenericBuffer* destination);

size_t GHDFArray_GetElementCount(GHDFArray* self);

GHDFValueType GHDFArray_GetElementType(GHDFArray* self);

/* Enumerates GHDFArrayIndexedValue values. */
ICollection* GHDFArray_GetElementCollection(GHDFArray* self);


Error GHDFObjectPool_Create(GHDFObjectPool** outPool);

Error GHDFObjectPool_Deconstruct(GHDFObjectPool* self);

Error GHDFObjectPool_BorrowCompound(GHDFObjectPool* self, GHDFCompound** outCompound);

Error GHDFObjectPool_BorrowArray(GHDFObjectPool* self, GHDFValueType elementType, GHDFArray** outArray);

Error GHDFObjectPool_BorrowString(GHDFObjectPool* self, GenericBuffer** outStringBuffer);

Error GHDFObjectPool_ReturnCompound(GHDFObjectPool* self, GHDFCompound* compound, bool includeNestedStructures);

Error GHDFObjectPool_ReturnArray(GHDFObjectPool* self, GHDFArray* array, bool includeNestedStructures);

Error GHDFObjectPool_ReturnString(GHDFObjectPool* self, GenericBuffer* stringBuffer);

GHDFCompoundEntryType GHDF_CreateRegularType(GHDFValueType valueType);

GHDFCompoundEntryType GHDF_CreateArrayType(GHDFValueType valueType);
