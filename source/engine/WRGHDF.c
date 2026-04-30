#include "WRGHDF.h"
#include "WRArrayList.h"
#include "WRBinaryIO.h"
#include "WRHash.h"
#include "WRHashMap.h"
#include "WRObjectPool.h"
#include "WRMap.h"
#include <inttypes.h>


// Macros.
#define GHDF_TYPE_ARRAY_BIT ((uint8_t)0x80U)
#define GHDF_VERSION ((uint64_t)1U)
#define GHDF_POOL_SECTION_CAPACITY ((size_t)16U)


// Types.
typedef struct GHDFEntryMetadataStruct
{
    bool IsArray;
    GenericBuffer* OwnedStringBuffer;
} GHDFEntryMetadata;

typedef struct GHDFArrayElementMetadataStruct
{
    GenericBuffer* OwnedStringBuffer;
} GHDFArrayElementMetadata;

typedef struct GHDFCompoundStruct
{
    HashMap _entries;
    HashMap _entryMetadata;
    GHDFObjectPool* _ownerPool;
} GHDFCompound;

typedef struct GHDFArrayStruct
{
    ArrayList _values;
    ArrayList _valueMetadata;
    GHDFValueType _elementType;
    GHDFObjectPool* _ownerPool;
} GHDFArray;

typedef struct GHDFObjectPoolStruct
{
    ObjectPool _compoundPool;
    ObjectPool _arrayPool;
    ObjectPool _stringPool;
} GHDFObjectPool;


// Fields.
static const unsigned char GHDF_SIGNATURE[16] =
{
    102U, 37U, 143U, 181U, 3U, 205U, 123U, 185U,
    148U, 157U, 98U, 177U, 178U, 151U, 43U, 170U
};


// Static functions.
static Error CreateNullArgumentError(const unsigned char* argumentName)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"GHDF argument \"%s\" must not be null.",
        argumentName);
}

static Error CreateInvalidEntryIDError(void)
{
    return Error_Construct1(ErrorCode_IllegalArgument, u8"GHDF entry ID 0 is invalid.");
}

static Error CreateEntryNotFoundError(GHDFEntryID id)
{
    return Error_Construct3(ErrorCode_InvalidOperation,
        u8"GHDF entry %" PRIu64 " was not found.",
        (uint64_t)id);
}

static Error CreateInvalidTypeError(GHDFValueType valueType)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"GHDF value type %d is invalid in this context.",
        (int)valueType);
}

static Error CreateTypeMismatchError(GHDFValueType actualType, GHDFCompoundEntryType expectedType, bool actualIsArray)
{
    return Error_Construct3(ErrorCode_InvalidOperation,
        u8"GHDF entry type mismatch. Actual type %d (array=%d), expected %d (array=%d).",
        (int)actualType,
        actualIsArray ? 1 : 0,
        (int)expectedType.ValueType,
        expectedType.IsArray ? 1 : 0);
}

static Error CreateInvalidWireTypeError(uint8_t typeByte)
{
    return Error_Construct3(ErrorCode_Deserialize,
        u8"GHDF type byte %u is invalid.",
        (unsigned int)typeByte);
}

static Error CreateInvalidVersionError(uint64_t version)
{
    return Error_Construct3(ErrorCode_Deserialize,
        u8"Unsupported GHDF version %" PRIu64 ".",
        version);
}

static Error CreateInvalidSignatureError(void)
{
    return Error_Construct1(ErrorCode_Deserialize, u8"Invalid GHDF signature.");
}

static Error CreateLengthRangeError(uint64_t length, const unsigned char* kindName)
{
    return Error_Construct3(ErrorCode_Deserialize,
        u8"GHDF %s length %" PRIu64 " exceeds the supported platform range.",
        kindName,
        length);
}

static Error CreateInvalidArrayValueError(void)
{
    return Error_Construct1(ErrorCode_IllegalArgument,
        u8"GHDF arrays cannot contain array entries.");
}

static size_t GetUTF8ByteCount(const unsigned char* text)
{
    size_t ByteCount = 0U;

    if (text == NULL)
    {
        return 0U;
    }

    while (text[ByteCount] != 0U)
    {
        ByteCount++;
    }

    return ByteCount;
}

static HashCode GHDFEntryIDHash(IMap* map, const void* key, void* userData)
{
    const GHDFEntryID* KeyValue = key;
    (void)map;
    (void)userData;
    return Hash_UInt64(*KeyValue);
}

static bool ValidateContainerValueType(GHDFValueType valueType)
{
    return (valueType >= GHDFValueType_UInt8) && (valueType <= GHDFValueType_EncodedInteger);
}

static Error ValidateCompoundEntryType(GHDFCompoundEntryType entryType)
{
    if (!ValidateContainerValueType(entryType.ValueType))
    {
        return CreateInvalidTypeError(entryType.ValueType);
    }

    return Error_CreateSuccess();
}

static Error ValidateArrayElementType(GHDFValueType elementType)
{
    if (!ValidateContainerValueType(elementType))
    {
        return CreateInvalidTypeError(elementType);
    }

    return Error_CreateSuccess();
}

static Error ValidateSizeOut(size_t* outValue, const unsigned char* argumentName)
{
    if (outValue == NULL)
    {
        return CreateNullArgumentError(argumentName);
    }

    return Error_CreateSuccess();
}

static Error ConvertUInt64ToSize(uint64_t value, const unsigned char* kindName, size_t* outValue)
{
    Error Result = ValidateSizeOut(outValue, u8"outValue");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (value > (uint64_t)SIZE_MAX)
    {
        return CreateLengthRangeError(value, kindName);
    }

    *outValue = (size_t)value;
    return Error_CreateSuccess();
}

static GHDFEntryMetadata GHDFEntryMetadata_Create(bool isArray, GenericBuffer* ownedStringBuffer)
{
    return (GHDFEntryMetadata) {
        .IsArray = isArray,
        .OwnedStringBuffer = ownedStringBuffer,
    };
}

static GHDFArrayElementMetadata GHDFArrayElementMetadata_Create(GenericBuffer* ownedStringBuffer)
{
    return (GHDFArrayElementMetadata) {
        .OwnedStringBuffer = ownedStringBuffer,
    };
}

static void GHDFCreateTempByteBuffer(GenericBuffer* buffer, unsigned char* data, size_t capacity)
{
    GenericBuffer_CreateVariable(buffer,
        data,
        capacity,
        sizeof(unsigned char),
        0,
        NULL,
        NULL);
}

static void GHDFObjectValue_Zero(GHDFObjectValue* value)
{
    if (value == NULL)
    {
        return;
    }

    Memory_Zero(value, sizeof(*value));
}

static Error GHDFHashMap_Construct(HashMap* map, size_t valueSize)
{
    HashMapConstructOptions Options = HashMapConstructOptions_CreateDefault(sizeof(GHDFEntryID), valueSize, &GHDFEntryIDHash);

    if (map == NULL)
    {
        return CreateNullArgumentError(u8"map");
    }

    return HashMap_Construct1(map, Options);
}

static Error GHDFCompound_Initialize(GHDFCompound* self)
{
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    Memory_Zero(self, sizeof(*self));
    Result = GHDFHashMap_Construct(&self->_entries, sizeof(GHDFObjectValue));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = GHDFHashMap_Construct(&self->_entryMetadata, sizeof(GHDFEntryMetadata));
    if (Result.Code != ErrorCode_Success)
    {
        (void)HashMap_Deconstruct(&self->_entries);
        Memory_Zero(self, sizeof(*self));
        return Result;
    }

    self->_ownerPool = NULL;
    return Error_CreateSuccess();
}

static Error GHDFArray_Initialize(GHDFArray* self, GHDFValueType elementType)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    Memory_Zero(self, sizeof(*self));
    ArrayList_Construct1(&self->_values, sizeof(GHDFObjectValue));
    ArrayList_Construct1(&self->_valueMetadata, sizeof(GHDFArrayElementMetadata));
    self->_elementType = elementType;
    self->_ownerPool = NULL;
    return Error_CreateSuccess();
}

static Error GHDFStringBuffer_ConstructObject(void* object, void* userData)
{
    GenericBuffer* Buffer = object;
    (void)userData;

    if (Buffer == NULL)
    {
        return CreateNullArgumentError(u8"object");
    }

    GenericBuffer_AllocateVariable(Buffer, 0, sizeof(unsigned char));
    return Error_CreateSuccess();
}

static Error GHDFStringBuffer_ResetObject(void* object, void* userData)
{
    GenericBuffer* Buffer = object;
    (void)userData;

    if (Buffer == NULL)
    {
        return CreateNullArgumentError(u8"object");
    }
    if (!GenericBuffer_Clear(Buffer))
    {
        return Error_Construct1(ErrorCode_InvalidOperation,
            u8"Could not clear a GHDF string buffer.");
    }

    return Error_CreateSuccess();
}

static Error GHDFStringBuffer_DeconstructObject(void* object, void* userData)
{
    GenericBuffer* Buffer = object;
    (void)userData;

    if (Buffer == NULL)
    {
        return CreateNullArgumentError(u8"object");
    }

    Memory_Free(Buffer->_data);
    Memory_Zero(Buffer, sizeof(*Buffer));
    return Error_CreateSuccess();
}

static Error GHDFCompoundPool_ConstructObject(void* object, void* userData)
{
    GHDFCompound* Compound = object;
    GHDFObjectPool* Pool = userData;
    Error Result = GHDFCompound_Initialize(Compound);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Compound->_ownerPool = Pool;
    return Error_CreateSuccess();
}

static Error GHDFArrayPool_ConstructObject(void* object, void* userData)
{
    GHDFArray* Array = object;
    GHDFObjectPool* Pool = userData;
    Error Result = GHDFArray_Initialize(Array, GHDFValueType_None);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Array->_ownerPool = Pool;
    return Error_CreateSuccess();
}

static Error GHDFCompoundPool_ResetObject(void* object, void* userData)
{
    GHDFCompound* Compound = object;
    GHDFObjectPool* Pool = userData;
    Error Result = GHDFCompound_Clear(Compound);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Compound->_ownerPool = Pool;
    return Error_CreateSuccess();
}

static Error GHDFCompoundPool_DeconstructObject(void* object, void* userData)
{
    (void)userData;
    return GHDFCompound_Deconstruct(object);
}

static Error GHDFArrayPool_ResetObject(void* object, void* userData)
{
    GHDFArray* Array = object;
    GHDFObjectPool* Pool = userData;
    Error Result = GHDFArray_Clear(Array);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Array->_ownerPool = Pool;
    Array->_elementType = GHDFValueType_None;
    return Error_CreateSuccess();
}

static Error GHDFArrayPool_DeconstructObject(void* object, void* userData)
{
    (void)userData;
    return GHDFArray_Deconstruct(object);
}

static Error GHDFMetadataMap_Get(GHDFCompound* self, GHDFEntryID id, GHDFEntryMetadata* outMetadata)
{
    Error Result = Error_CreateSuccess();

    if (outMetadata == NULL)
    {
        return CreateNullArgumentError(u8"outMetadata");
    }

    Result = IMap_GetElement(HashMap_AsMap(&self->_entryMetadata), &id, outMetadata);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Error_Construct3(ErrorCode_InvalidState,
            u8"Missing GHDF metadata for entry %" PRIu64 ".",
            (uint64_t)id);
    }

    return Error_CreateSuccess();
}

static Error GHDFEntryMetadata_Set(GHDFCompound* self, GHDFEntryID id, GHDFEntryMetadata metadata)
{
    bool WasAdded = false;

    return IMap_Add(HashMap_AsMap(&self->_entryMetadata), &id, &metadata, &WasAdded);
}

static Error GHDFObjectPool_ReturnOwnedStringBuffer(GHDFObjectPool* self, GenericBuffer* stringBuffer)
{
    if (stringBuffer == NULL)
    {
        return Error_CreateSuccess();
    }
    if (self == NULL)
    {
        return Error_Construct1(ErrorCode_InvalidState,
            u8"Cannot return a GHDF string buffer without its owner pool.");
    }

    return GHDFObjectPool_ReturnString(self, stringBuffer);
}

static Error GHDFCompound_ReleaseEntryResources(GHDFCompound* self, GHDFEntryID id)
{
    GHDFEntryMetadata Metadata;
    Error Result = Error_CreateSuccess();

    Result = GHDFMetadataMap_Get(self, id, &Metadata);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Error_CreateSuccess();
    }

    if (Metadata.OwnedStringBuffer != NULL)
    {
        return GHDFObjectPool_ReturnOwnedStringBuffer(self->_ownerPool, Metadata.OwnedStringBuffer);
    }

    return Error_CreateSuccess();
}

static Error GHDFArray_ReleaseElementResources(GHDFArray* self, size_t index)
{
    GHDFArrayElementMetadata Metadata;
    Error Result = Error_CreateSuccess();

    Result = IList_GetElement(&self->_valueMetadata._list, index, &Metadata);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    if (Metadata.OwnedStringBuffer != NULL)
    {
        return GHDFObjectPool_ReturnOwnedStringBuffer(self->_ownerPool, Metadata.OwnedStringBuffer);
    }

    return Error_CreateSuccess();
}

static Error GHDFCompound_ReturnExistingNestedValue(GHDFCompound* self, GHDFEntryID id)
{
    GHDFObjectValue ExistingValue;
    GHDFEntryMetadata Metadata;
    Error Result = Error_CreateSuccess();

    if ((self == NULL) || (self->_ownerPool == NULL))
    {
        return Error_CreateSuccess();
    }

    Result = IMap_GetElement(HashMap_AsMap(&self->_entries), &id, &ExistingValue);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Error_CreateSuccess();
    }

    Result = GHDFMetadataMap_Get(self, id, &Metadata);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Error_CreateSuccess();
    }

    if (Metadata.IsArray && (ExistingValue.Value.Array != NULL))
    {
        return GHDFObjectPool_ReturnArray(self->_ownerPool, ExistingValue.Value.Array, true);
    }
    if ((ExistingValue.Type == GHDFValueType_Compound) && (ExistingValue.Value.Compound != NULL))
    {
        return GHDFObjectPool_ReturnCompound(self->_ownerPool, ExistingValue.Value.Compound, true);
    }

    return Error_CreateSuccess();
}

static Error GHDFArray_ReturnExistingNestedValue(GHDFArray* self, size_t index)
{
    GHDFObjectValue ExistingValue;
    Error Result = Error_CreateSuccess();

    if ((self == NULL) || (self->_ownerPool == NULL) || (self->_elementType != GHDFValueType_Compound))
    {
        return Error_CreateSuccess();
    }

    Result = IList_GetElement(&self->_values._list, index, &ExistingValue);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (ExistingValue.Value.Compound != NULL)
    {
        return GHDFObjectPool_ReturnCompound(self->_ownerPool, ExistingValue.Value.Compound, true);
    }

    return Error_CreateSuccess();
}

static Error GHDFArray_InsertValueWithMetadata(GHDFArray* self,
    size_t index,
    GHDFObjectValue value,
    GHDFArrayElementMetadata metadata)
{
    Error Result = IList_Insert(&self->_values._list, index, &value);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = IList_Insert(&self->_valueMetadata._list, index, &metadata);
    if (Result.Code != ErrorCode_Success)
    {
        (void)IList_RemoveAt(&self->_values._list, index);
        return Result;
    }

    return Error_CreateSuccess();
}

static Error GHDFArray_AddValueWithMetadata(GHDFArray* self, GHDFObjectValue value, GHDFArrayElementMetadata metadata)
{
    return GHDFArray_InsertValueWithMetadata(self, IList_GetElementCount(&self->_values._list), value, metadata);
}

static Error GHDFArray_ReplaceValueWithMetadata(GHDFArray* self,
    size_t index,
    GHDFObjectValue value,
    GHDFArrayElementMetadata metadata)
{
    Error Result = GHDFArray_ReturnExistingNestedValue(self, index);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = GHDFArray_ReleaseElementResources(self, index);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = IList_Replace(&self->_values._list, index, &value);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return IList_Replace(&self->_valueMetadata._list, index, &metadata);
}

static Error GHDFCompound_SetEntryValue(GHDFCompound* self,
    GHDFEntryID id,
    GHDFCompoundEntryType entryType,
    GHDFObjectValue value,
    GenericBuffer* ownedStringBuffer)
{
    GHDFEntryMetadata Metadata = GHDFEntryMetadata_Create(entryType.IsArray, ownedStringBuffer);
    bool WasAdded = false;
    Error Result = Error_CreateSuccess();

    Result = GHDFCompound_ReturnExistingNestedValue(self, id);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = GHDFCompound_ReleaseEntryResources(self, id);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = IMap_Add(HashMap_AsMap(&self->_entries), &id, &value, &WasAdded);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return GHDFEntryMetadata_Set(self, id, Metadata);
}

static Error GHDFObjectValue_FromScalar(GHDFValueType valueType, void* value, GHDFObjectValue* outValue)
{
    if (outValue == NULL)
    {
        return CreateNullArgumentError(u8"outValue");
    }
    if (value == NULL)
    {
        return CreateNullArgumentError(u8"value");
    }

    GHDFObjectValue_Zero(outValue);
    outValue->Type = valueType;
    switch (valueType)
    {
        case GHDFValueType_UInt8:
            outValue->Value.UInt8 = *((uint8_t*)value);
            return Error_CreateSuccess();

        case GHDFValueType_Int8:
            outValue->Value.Int8 = *((int8_t*)value);
            return Error_CreateSuccess();

        case GHDFValueType_Int16:
            outValue->Value.Int16 = *((int16_t*)value);
            return Error_CreateSuccess();

        case GHDFValueType_UInt16:
            outValue->Value.UInt16 = *((uint16_t*)value);
            return Error_CreateSuccess();

        case GHDFValueType_Int32:
            outValue->Value.Int32 = *((int32_t*)value);
            return Error_CreateSuccess();

        case GHDFValueType_UInt32:
            outValue->Value.UInt32 = *((uint32_t*)value);
            return Error_CreateSuccess();

        case GHDFValueType_Int64:
            outValue->Value.Int64 = *((int64_t*)value);
            return Error_CreateSuccess();

        case GHDFValueType_UInt64:
            outValue->Value.UInt64 = *((uint64_t*)value);
            return Error_CreateSuccess();

        case GHDFValueType_Float:
            outValue->Value.Float = *((float*)value);
            return Error_CreateSuccess();

        case GHDFValueType_Double:
            outValue->Value.Double = *((double*)value);
            return Error_CreateSuccess();

        case GHDFValueType_Boolean:
            outValue->Value.Boolean = *((bool*)value);
            return Error_CreateSuccess();

        case GHDFValueType_String:
            outValue->Value.String = value;
            return Error_CreateSuccess();

        case GHDFValueType_Compound:
            outValue->Value.Compound = value;
            return Error_CreateSuccess();

        case GHDFValueType_EncodedInteger:
            outValue->Value.EncodedInteger = *((int64_t*)value);
            return Error_CreateSuccess();

        case GHDFValueType_None:
        default:
            return CreateInvalidTypeError(valueType);
    }
}

static Error GHDFWriteExactBytes(BinaryIOStream* stream, const unsigned char* bytes, size_t byteCount)
{
    if (stream == NULL)
    {
        return CreateNullArgumentError(u8"stream");
    }
    if ((bytes == NULL) && (byteCount > 0U))
    {
        return CreateNullArgumentError(u8"bytes");
    }

    return IOStream_Write(&stream->Base, bytes, byteCount);
}

static Error GHDFReadExactBytes(BinaryIOStream* stream, GenericBuffer* destination, size_t byteCount)
{
    size_t StartingCount = 0U;
    Error Result = Error_CreateSuccess();

    if (stream == NULL)
    {
        return CreateNullArgumentError(u8"stream");
    }
    if (destination == NULL)
    {
        return CreateNullArgumentError(u8"destination");
    }

    StartingCount = destination->_count;
    Result = IOStream_Read(&stream->Base, byteCount, destination);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((destination->_count - StartingCount) != byteCount)
    {
        return Error_Construct3(ErrorCode_Deserialize,
            u8"Unexpected end of GHDF data while reading %zu bytes.",
            byteCount);
    }

    return Error_CreateSuccess();
}

static Error GHDF_WriteSignature(BinaryIOStream* stream)
{
    return GHDFWriteExactBytes(stream, GHDF_SIGNATURE, sizeof(GHDF_SIGNATURE));
}

static Error GHDF_ReadSignature(BinaryIOStream* stream)
{
    unsigned char SignatureBytes[sizeof(GHDF_SIGNATURE)];
    GenericBuffer Buffer;
    Error Result = Error_CreateSuccess();

    GHDFCreateTempByteBuffer(&Buffer, SignatureBytes, sizeof(SignatureBytes));
    Result = GHDFReadExactBytes(stream, &Buffer, sizeof(SignatureBytes));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!Memory_IsEqual(SignatureBytes, GHDF_SIGNATURE, sizeof(GHDF_SIGNATURE)))
    {
        return CreateInvalidSignatureError();
    }

    return Error_CreateSuccess();
}

static Error GHDF_WriteVersion(BinaryIOStream* stream)
{
    return BinaryIOStream_WriteEncodedUInt64(stream, GHDF_VERSION);
}

static Error GHDF_ReadVersion(BinaryIOStream* stream)
{
    uint64_t Version = 0U;
    Error Result = BinaryIOStream_ReadEncodedUInt64(stream, &Version);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (Version != GHDF_VERSION)
    {
        return CreateInvalidVersionError(Version);
    }

    return Error_CreateSuccess();
}

static uint8_t GHDF_ComposeTypeByte(GHDFCompoundEntryType entryType)
{
    uint8_t TypeByte = (uint8_t)entryType.ValueType;

    if (entryType.IsArray)
    {
        TypeByte = (uint8_t)(TypeByte | GHDF_TYPE_ARRAY_BIT);
    }

    return TypeByte;
}

static Error GHDF_ParseTypeByte(uint8_t typeByte, GHDFCompoundEntryType* outEntryType)
{
    GHDFValueType BaseType = (GHDFValueType)(typeByte & (~GHDF_TYPE_ARRAY_BIT));

    if (outEntryType == NULL)
    {
        return CreateNullArgumentError(u8"outEntryType");
    }
    if (!ValidateContainerValueType(BaseType))
    {
        return CreateInvalidWireTypeError(typeByte);
    }

    outEntryType->ValueType = BaseType;
    outEntryType->IsArray = (typeByte & GHDF_TYPE_ARRAY_BIT) != 0U;
    return Error_CreateSuccess();
}

static Error GHDF_WriteStringValue(BinaryIOStream* stream, const unsigned char* text)
{
    size_t ByteCount = 0U;
    Error Result = Error_CreateSuccess();

    if (text == NULL)
    {
        return CreateNullArgumentError(u8"text");
    }

    ByteCount = GetUTF8ByteCount(text);
    Result = BinaryIOStream_WriteEncodedUInt64(stream, (uint64_t)ByteCount);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return GHDFWriteExactBytes(stream, text, ByteCount);
}

static Error GHDF_ReadOwnedStringValue(BinaryIOStream* stream,
    GHDFObjectPool* objectPool,
    unsigned char** outString,
    GenericBuffer** outStringBuffer)
{
    uint64_t Length64 = 0U;
    size_t Length = 0U;
    GenericBuffer* StringBuffer = NULL;
    Error Result = Error_CreateSuccess();

    if (outString == NULL)
    {
        return CreateNullArgumentError(u8"outString");
    }
    if (outStringBuffer == NULL)
    {
        return CreateNullArgumentError(u8"outStringBuffer");
    }

    *outString = NULL;
    *outStringBuffer = NULL;
    Result = BinaryIOStream_ReadEncodedUInt64(stream, &Length64);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ConvertUInt64ToSize(Length64, u8"string", &Length);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = GHDFObjectPool_BorrowString(objectPool, &StringBuffer);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!GenericBuffer_TryPrepareForManualMutation(StringBuffer, Length + 1U))
    {
        (void)GHDFObjectPool_ReturnString(objectPool, StringBuffer);
        return Error_Construct3(ErrorCode_BufferTooSmall,
            u8"Could not allocate %zu bytes for a GHDF string.",
            Length + 1U);
    }

    Result = GHDFReadExactBytes(stream, StringBuffer, Length);
    if (Result.Code != ErrorCode_Success)
    {
        (void)GHDFObjectPool_ReturnString(objectPool, StringBuffer);
        return Result;
    }
    if (!GenericBuffer_NullTerminate(StringBuffer))
    {
        (void)GHDFObjectPool_ReturnString(objectPool, StringBuffer);
        return Error_Construct1(ErrorCode_BufferTooSmall,
            u8"Could not null-terminate a GHDF string.");
    }

    *outString = StringBuffer->_data;
    *outStringBuffer = StringBuffer;
    return Error_CreateSuccess();
}

static Error GHDF_WriteScalarValue(BinaryIOStream* stream, GHDFObjectValue value)
{
    switch (value.Type)
    {
        case GHDFValueType_UInt8:
            return BinaryIOStream_WriteUInt8(stream, value.Value.UInt8);

        case GHDFValueType_Int8:
            return BinaryIOStream_WriteInt8(stream, value.Value.Int8);

        case GHDFValueType_Int16:
            return BinaryIOStream_WriteInt16(stream, value.Value.Int16);

        case GHDFValueType_UInt16:
            return BinaryIOStream_WriteUInt16(stream, value.Value.UInt16);

        case GHDFValueType_Int32:
            return BinaryIOStream_WriteInt32(stream, value.Value.Int32);

        case GHDFValueType_UInt32:
            return BinaryIOStream_WriteUInt32(stream, value.Value.UInt32);

        case GHDFValueType_Int64:
            return BinaryIOStream_WriteInt64(stream, value.Value.Int64);

        case GHDFValueType_UInt64:
            return BinaryIOStream_WriteUInt64(stream, value.Value.UInt64);

        case GHDFValueType_Float:
            return BinaryIOStream_WriteFloat(stream, value.Value.Float);

        case GHDFValueType_Double:
            return BinaryIOStream_WriteDouble(stream, value.Value.Double);

        case GHDFValueType_Boolean:
            return BinaryIOStream_WriteBoolean(stream, value.Value.Boolean);

        case GHDFValueType_String:
            return GHDF_WriteStringValue(stream, value.Value.String);

        case GHDFValueType_EncodedInteger:
            return BinaryIOStream_WriteEncodedInt64(stream, value.Value.EncodedInteger);

        case GHDFValueType_Compound:
        case GHDFValueType_None:
        default:
            return CreateInvalidTypeError(value.Type);
    }
}

static Error GHDF_ReadScalarValue(BinaryIOStream* stream,
    GHDFValueType valueType,
    GHDFObjectPool* objectPool,
    GHDFObjectValue* outValue,
    GenericBuffer** outOwnedStringBuffer);

static Error GHDF_WriteArray(BinaryIOStream* stream, GHDFArray* array);

static Error GHDF_WriteCompoundBody(BinaryIOStream* stream, const GHDFCompound* compound);

static Error GHDF_ReadCompoundBody(BinaryIOStream* stream, GHDFObjectPool* objectPool, GHDFCompound* compound);

static Error GHDF_ReadArray(BinaryIOStream* stream,
    GHDFObjectPool* objectPool,
    GHDFValueType elementType,
    GHDFArray** outArray);

static Error GHDFObjectPool_ReturnNestedFromCompound(GHDFObjectPool* objectPool, GHDFCompound* compound);

static Error GHDFObjectPool_ReturnNestedFromArray(GHDFObjectPool* objectPool, GHDFArray* array);

static Error GHDFCompound_ReturnExistingNestedValue(GHDFCompound* self, GHDFEntryID id);

static Error GHDFArray_ReturnExistingNestedValue(GHDFArray* self, size_t index);

static Error GHDF_ReadScalarValue(BinaryIOStream* stream,
    GHDFValueType valueType,
    GHDFObjectPool* objectPool,
    GHDFObjectValue* outValue,
    GenericBuffer** outOwnedStringBuffer)
{
    if (outValue == NULL)
    {
        return CreateNullArgumentError(u8"outValue");
    }
    if (outOwnedStringBuffer == NULL)
    {
        return CreateNullArgumentError(u8"outOwnedStringBuffer");
    }

    *outOwnedStringBuffer = NULL;
    GHDFObjectValue_Zero(outValue);
    outValue->Type = valueType;
    switch (valueType)
    {
        case GHDFValueType_UInt8:
            return BinaryIOStream_ReadUInt8(stream, &outValue->Value.UInt8);

        case GHDFValueType_Int8:
            return BinaryIOStream_ReadInt8(stream, &outValue->Value.Int8);

        case GHDFValueType_Int16:
            return BinaryIOStream_ReadInt16(stream, &outValue->Value.Int16);

        case GHDFValueType_UInt16:
            return BinaryIOStream_ReadUInt16(stream, &outValue->Value.UInt16);

        case GHDFValueType_Int32:
            return BinaryIOStream_ReadInt32(stream, &outValue->Value.Int32);

        case GHDFValueType_UInt32:
            return BinaryIOStream_ReadUInt32(stream, &outValue->Value.UInt32);

        case GHDFValueType_Int64:
            return BinaryIOStream_ReadInt64(stream, &outValue->Value.Int64);

        case GHDFValueType_UInt64:
            return BinaryIOStream_ReadUInt64(stream, &outValue->Value.UInt64);

        case GHDFValueType_Float:
            return BinaryIOStream_ReadFloat(stream, &outValue->Value.Float);

        case GHDFValueType_Double:
            return BinaryIOStream_ReadDouble(stream, &outValue->Value.Double);

        case GHDFValueType_Boolean:
            return BinaryIOStream_ReadBoolean(stream, &outValue->Value.Boolean);

        case GHDFValueType_String:
            return GHDF_ReadOwnedStringValue(stream, objectPool, &outValue->Value.String, outOwnedStringBuffer);

        case GHDFValueType_EncodedInteger:
            return BinaryIOStream_ReadEncodedInt64(stream, &outValue->Value.EncodedInteger);

        case GHDFValueType_Compound:
        case GHDFValueType_None:
        default:
            return CreateInvalidTypeError(valueType);
    }
}

static Error GHDF_WriteEntryValue(BinaryIOStream* stream,
    GHDFCompoundEntryType entryType,
    GHDFObjectValue value)
{
    if (entryType.IsArray)
    {
        if (value.Value.Array == NULL)
        {
            return CreateNullArgumentError(u8"value.Array");
        }

        return GHDF_WriteArray(stream, value.Value.Array);
    }
    if (entryType.ValueType == GHDFValueType_Compound)
    {
        if (value.Value.Compound == NULL)
        {
            return CreateNullArgumentError(u8"value.Compound");
        }

        return GHDF_WriteCompoundBody(stream, value.Value.Compound);
    }

    return GHDF_WriteScalarValue(stream, value);
}

static Error GHDF_ReadEntryValue(BinaryIOStream* stream,
    GHDFObjectPool* objectPool,
    GHDFCompoundEntryType entryType,
    GHDFObjectValue* outValue,
    GenericBuffer** outOwnedStringBuffer)
{
    Error Result = Error_CreateSuccess();

    if (outValue == NULL)
    {
        return CreateNullArgumentError(u8"outValue");
    }
    if (outOwnedStringBuffer == NULL)
    {
        return CreateNullArgumentError(u8"outOwnedStringBuffer");
    }

    *outOwnedStringBuffer = NULL;
    GHDFObjectValue_Zero(outValue);
    outValue->Type = entryType.ValueType;
    if (entryType.IsArray)
    {
        return GHDF_ReadArray(stream, objectPool, entryType.ValueType, &outValue->Value.Array);
    }
    if (entryType.ValueType == GHDFValueType_Compound)
    {
        GHDFCompound* Compound = NULL;

        Result = GHDFObjectPool_BorrowCompound(objectPool, &Compound);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        Result = GHDF_ReadCompoundBody(stream, objectPool, Compound);
        if (Result.Code != ErrorCode_Success)
        {
            (void)GHDFObjectPool_ReturnCompound(objectPool, Compound, true);
            return Result;
        }

        outValue->Value.Compound = Compound;
        return Error_CreateSuccess();
    }

    return GHDF_ReadScalarValue(stream, entryType.ValueType, objectPool, outValue, outOwnedStringBuffer);
}

static Error GHDF_WriteArray(BinaryIOStream* stream, GHDFArray* array)
{
    size_t ElementCount = 0U;

    if (array == NULL)
    {
        return CreateNullArgumentError(u8"array");
    }

    ElementCount = IList_GetElementCount(&array->_values._list);
    if (array->_elementType == GHDFValueType_None)
    {
        return CreateInvalidTypeError(array->_elementType);
    }

    Error Result = BinaryIOStream_WriteEncodedUInt64(stream, (uint64_t)ElementCount);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    for (size_t Index = 0; Index < ElementCount; Index++)
    {
        GHDFObjectValue Value;

        Result = IList_GetElement(&array->_values._list, Index, &Value);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        if (array->_elementType == GHDFValueType_Compound)
        {
            Result = GHDF_WriteCompoundBody(stream, Value.Value.Compound);
        }
        else
        {
            Result = GHDF_WriteScalarValue(stream, Value);
        }
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    return Error_CreateSuccess();
}

static Error GHDF_ReadArray(BinaryIOStream* stream,
    GHDFObjectPool* objectPool,
    GHDFValueType elementType,
    GHDFArray** outArray)
{
    uint64_t ElementCount64 = 0U;
    size_t ElementCount = 0U;
    GHDFArray* Array = NULL;
    Error Result = Error_CreateSuccess();

    if (outArray == NULL)
    {
        return CreateNullArgumentError(u8"outArray");
    }

    *outArray = NULL;
    Result = BinaryIOStream_ReadEncodedUInt64(stream, &ElementCount64);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = ConvertUInt64ToSize(ElementCount64, u8"array", &ElementCount);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = GHDFObjectPool_BorrowArray(objectPool, &Array);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Array->_elementType = elementType;
    for (size_t Index = 0; Index < ElementCount; Index++)
    {
        GHDFObjectValue Value;
        GenericBuffer* OwnedStringBuffer = NULL;

        if (elementType == GHDFValueType_Compound)
        {
            Value.Type = GHDFValueType_Compound;
            Result = GHDFObjectPool_BorrowCompound(objectPool, &Value.Value.Compound);
            if (Result.Code != ErrorCode_Success)
            {
                (void)GHDFObjectPool_ReturnArray(objectPool, Array, true);
                return Result;
            }

            Result = GHDF_ReadCompoundBody(stream, objectPool, Value.Value.Compound);
        }
        else
        {
            Result = GHDF_ReadScalarValue(stream, elementType, objectPool, &Value, &OwnedStringBuffer);
        }
        if (Result.Code != ErrorCode_Success)
        {
            (void)GHDFObjectPool_ReturnArray(objectPool, Array, true);
            return Result;
        }

        Result = GHDFArray_AddValueWithMetadata(Array, Value, GHDFArrayElementMetadata_Create(OwnedStringBuffer));
        if (Result.Code != ErrorCode_Success)
        {
            if (OwnedStringBuffer != NULL)
            {
                (void)GHDFObjectPool_ReturnString(objectPool, OwnedStringBuffer);
            }
            (void)GHDFObjectPool_ReturnArray(objectPool, Array, true);
            return Result;
        }
    }

    *outArray = Array;
    return Error_CreateSuccess();
}

static Error GHDF_WriteCompoundBody(BinaryIOStream* stream, const GHDFCompound* compound)
{
    CollectionEnumerator* Enumerator = NULL;
    Error Result = Error_CreateSuccess();
    GHDFCompound* MutableCompound = (GHDFCompound*)compound;

    if (compound == NULL)
    {
        return CreateNullArgumentError(u8"compound");
    }

    Result = BinaryIOStream_WriteEncodedUInt64(stream, (uint64_t)IMap_GetEntryCount(HashMap_AsMap(&MutableCompound->_entries)));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Enumerator = ICollection_GetEnumerator(IMap_AsEntryCollection(HashMap_AsMap(&MutableCompound->_entries)));
    if (Enumerator == NULL)
    {
        return Error_Construct1(ErrorCode_InvalidState,
            u8"Could not enumerate GHDF compound entries.");
    }

    while (true)
    {
        bool HasNext = false;
        MapEntryView EntryView;
        GHDFEntryMetadata Metadata;
        GHDFCompoundEntryType EntryType;

        Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }
        if (!HasNext)
        {
            Result = Error_CreateSuccess();
            break;
        }

        Result = CollectionEnumerator_NextByValue(Enumerator, &EntryView);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }

        Result = GHDFMetadataMap_Get(MutableCompound, *((GHDFEntryID*)EntryView._key), &Metadata);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }

        EntryType.ValueType = ((GHDFObjectValue*)EntryView._value)->Type;
        EntryType.IsArray = Metadata.IsArray;
        Result = BinaryIOStream_WriteEncodedUInt64(stream, *((GHDFEntryID*)EntryView._key));
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }

        Result = BinaryIOStream_WriteUInt8(stream, GHDF_ComposeTypeByte(EntryType));
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }

        Result = GHDF_WriteEntryValue(stream, EntryType, *((GHDFObjectValue*)EntryView._value));
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }
    }

    CollectionEnumerator_Deconstruct(Enumerator);
    return Result;
}

static Error GHDFObjectPool_ReturnNestedFromCompound(GHDFObjectPool* objectPool, GHDFCompound* compound)
{
    CollectionEnumerator* Enumerator = NULL;
    Error Result = Error_CreateSuccess();

    if ((objectPool == NULL) || (compound == NULL))
    {
        return Error_CreateSuccess();
    }

    Enumerator = ICollection_GetEnumerator(IMap_AsEntryCollection(HashMap_AsMap(&compound->_entries)));
    if (Enumerator == NULL)
    {
        return Error_CreateSuccess();
    }

    while (true)
    {
        bool HasNext = false;
        MapEntryView EntryView;
        GHDFEntryMetadata Metadata;

        Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }
        if (!HasNext)
        {
            Result = Error_CreateSuccess();
            break;
        }

        Result = CollectionEnumerator_NextByValue(Enumerator, &EntryView);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }

        Result = GHDFMetadataMap_Get(compound, *((GHDFEntryID*)EntryView._key), &Metadata);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }

        if (Metadata.IsArray)
        {
            GHDFArray* NestedArray = ((GHDFObjectValue*)EntryView._value)->Value.Array;

            if (NestedArray != NULL)
            {
                Result = GHDFObjectPool_ReturnArray(objectPool, NestedArray, true);
            }
        }
        else if (((GHDFObjectValue*)EntryView._value)->Type == GHDFValueType_Compound)
        {
            GHDFCompound* NestedCompound = ((GHDFObjectValue*)EntryView._value)->Value.Compound;

            if (NestedCompound != NULL)
            {
                Result = GHDFObjectPool_ReturnCompound(objectPool, NestedCompound, true);
            }
        }
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }
    }

    CollectionEnumerator_Deconstruct(Enumerator);
    return Result;
}

static Error GHDFObjectPool_ReturnNestedFromArray(GHDFObjectPool* objectPool, GHDFArray* array)
{
    size_t ElementCount = 0U;
    Error Result = Error_CreateSuccess();

    if ((objectPool == NULL) || (array == NULL) || (array->_elementType != GHDFValueType_Compound))
    {
        return Error_CreateSuccess();
    }

    ElementCount = IList_GetElementCount(&array->_values._list);
    for (size_t Index = 0; Index < ElementCount; Index++)
    {
        GHDFObjectValue Value;

        Result = IList_GetElement(&array->_values._list, Index, &Value);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        if (Value.Value.Compound != NULL)
        {
            Result = GHDFObjectPool_ReturnCompound(objectPool, Value.Value.Compound, true);
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }
        }
    }

    return Error_CreateSuccess();
}

static Error GHDF_ReadCompoundBody(BinaryIOStream* stream, GHDFObjectPool* objectPool, GHDFCompound* compound)
{
    uint64_t EntryCount64 = 0U;
    size_t EntryCount = 0U;
    Error Result = BinaryIOStream_ReadEncodedUInt64(stream, &EntryCount64);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ConvertUInt64ToSize(EntryCount64, u8"compound", &EntryCount);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    for (size_t Index = 0; Index < EntryCount; Index++)
    {
        GHDFEntryID EntryID = GHDF_ENTRY_ID_INVALID;
        uint8_t TypeByte = 0U;
        GHDFCompoundEntryType EntryType;
        GHDFObjectValue Value;
        GenericBuffer* OwnedStringBuffer = NULL;

        Result = BinaryIOStream_ReadEncodedUInt64(stream, &EntryID);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        if (EntryID == GHDF_ENTRY_ID_INVALID)
        {
            return CreateInvalidEntryIDError();
        }

        Result = BinaryIOStream_ReadUInt8(stream, &TypeByte);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        Result = GHDF_ParseTypeByte(TypeByte, &EntryType);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        Result = GHDF_ReadEntryValue(stream, objectPool, EntryType, &Value, &OwnedStringBuffer);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        Result = GHDFCompound_SetEntryValue(compound, EntryID, EntryType, Value, OwnedStringBuffer);
        if (Result.Code != ErrorCode_Success)
        {
            if (OwnedStringBuffer != NULL)
            {
                (void)GHDFObjectPool_ReturnString(objectPool, OwnedStringBuffer);
            }
            return Result;
        }
    }

    return Error_CreateSuccess();
}

static Error GHDFCompound_GetEntryMetadata(GHDFCompound* self,
    GHDFEntryID id,
    GHDFObjectValue* outEntry,
    GHDFEntryMetadata* outMetadata)
{
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outEntry == NULL)
    {
        return CreateNullArgumentError(u8"outEntry");
    }
    if (outMetadata == NULL)
    {
        return CreateNullArgumentError(u8"outMetadata");
    }

    Result = IMap_GetElement(HashMap_AsMap(&self->_entries), &id, outEntry);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return CreateEntryNotFoundError(id);
    }

    return GHDFMetadataMap_Get(self, id, outMetadata);
}

static Error GHDFArray_SetElementValue(GHDFArray* self,
    size_t index,
    GHDFObjectValue value,
    GenericBuffer* ownedStringBuffer,
    bool isReplace)
{
    GHDFArrayElementMetadata Metadata = GHDFArrayElementMetadata_Create(ownedStringBuffer);

    if (isReplace)
    {
        return GHDFArray_ReplaceValueWithMetadata(self, index, value, Metadata);
    }

    return GHDFArray_InsertValueWithMetadata(self, index, value, Metadata);
}

static Error GHDFArray_CreateValue(GHDFArray* self, void* element, GHDFObjectValue* outValue)
{
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    if (self->_elementType == GHDFValueType_None)
    {
        return CreateInvalidTypeError(self->_elementType);
    }
    if (self->_elementType == GHDFValueType_Compound)
    {
        if (outValue == NULL)
        {
            return CreateNullArgumentError(u8"outValue");
        }
        if (element == NULL)
        {
            return CreateNullArgumentError(u8"element");
        }

        GHDFObjectValue_Zero(outValue);
        outValue->Type = GHDFValueType_Compound;
        outValue->Value.Compound = element;
        return Error_CreateSuccess();
    }

    Result = GHDFObjectValue_FromScalar(self->_elementType, element, outValue);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    if (outValue->Type == GHDFValueType_Compound)
    {
        return CreateInvalidArrayValueError();
    }

    return Error_CreateSuccess();
}

static Error GHDFObjectPool_Construct(GHDFObjectPool* self)
{
    ObjectPoolLifecycle CompoundLifecycle =
    {
        .ConstructObject = &GHDFCompoundPool_ConstructObject,
        .ResetObject = &GHDFCompoundPool_ResetObject,
        .DeconstructObject = &GHDFCompoundPool_DeconstructObject,
    };
    ObjectPoolLifecycle ArrayLifecycle =
    {
        .ConstructObject = &GHDFArrayPool_ConstructObject,
        .ResetObject = &GHDFArrayPool_ResetObject,
        .DeconstructObject = &GHDFArrayPool_DeconstructObject,
    };
    ObjectPoolLifecycle StringLifecycle =
    {
        .ConstructObject = &GHDFStringBuffer_ConstructObject,
        .ResetObject = &GHDFStringBuffer_ResetObject,
        .DeconstructObject = &GHDFStringBuffer_DeconstructObject,
    };
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    Memory_Zero(self, sizeof(*self));
    Result = ObjectPool_Construct2(&self->_compoundPool,
        sizeof(GHDFCompound),
        GHDF_POOL_SECTION_CAPACITY,
        CompoundLifecycle,
        self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ObjectPool_Construct2(&self->_arrayPool,
        sizeof(GHDFArray),
        GHDF_POOL_SECTION_CAPACITY,
        ArrayLifecycle,
        self);
    if (Result.Code != ErrorCode_Success)
    {
        ObjectPool_Deconstruct(&self->_compoundPool);
        return Result;
    }

    Result = ObjectPool_Construct2(&self->_stringPool,
        sizeof(GenericBuffer),
        GHDF_POOL_SECTION_CAPACITY,
        StringLifecycle,
        self);
    if (Result.Code != ErrorCode_Success)
    {
        ObjectPool_Deconstruct(&self->_arrayPool);
        ObjectPool_Deconstruct(&self->_compoundPool);
        return Result;
    }

    return Error_CreateSuccess();
}


// Public functions.
Error GHDF_Write(const GHDFCompound* root, IOStream* stream)
{
    BinaryIOStream BinaryStream;
    Error Result = Error_CreateSuccess();

    if (root == NULL)
    {
        return CreateNullArgumentError(u8"root");
    }
    if (stream == NULL)
    {
        return CreateNullArgumentError(u8"stream");
    }

    Result = BinaryIOStream_Construct2(&BinaryStream, stream, MachineEndianess_LittleEndian, false);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = GHDF_WriteSignature(&BinaryStream);
    if (Result.Code == ErrorCode_Success)
    {
        Result = GHDF_WriteVersion(&BinaryStream);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = GHDF_WriteCompoundBody(&BinaryStream, root);
    }

    BinaryIOStream_Deconstruct(&BinaryStream);
    return Result;
}

Error GHDF_Read(IOStream* stream, GHDFObjectPool* objectPool, GHDFCompound** outRoot)
{
    BinaryIOStream BinaryStream;
    GHDFCompound* Root = NULL;
    Error Result = Error_CreateSuccess();

    if (stream == NULL)
    {
        return CreateNullArgumentError(u8"stream");
    }
    if (objectPool == NULL)
    {
        return CreateNullArgumentError(u8"objectPool");
    }
    if (outRoot == NULL)
    {
        return CreateNullArgumentError(u8"outRoot");
    }

    *outRoot = NULL;
    Result = BinaryIOStream_Construct2(&BinaryStream, stream, MachineEndianess_LittleEndian, false);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = GHDF_ReadSignature(&BinaryStream);
    if (Result.Code == ErrorCode_Success)
    {
        Result = GHDF_ReadVersion(&BinaryStream);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = GHDFObjectPool_BorrowCompound(objectPool, &Root);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = GHDF_ReadCompoundBody(&BinaryStream, objectPool, Root);
    }

    BinaryIOStream_Deconstruct(&BinaryStream);
    if (Result.Code != ErrorCode_Success)
    {
        if (Root != NULL)
        {
            (void)GHDFObjectPool_ReturnCompound(objectPool, Root, true);
        }
        return Result;
    }

    *outRoot = Root;
    return Error_CreateSuccess();
}

Error GHDFCompound_Construct1(GHDFCompound* self)
{
    return GHDFCompound_Initialize(self);
}

Error GHDFCompound_Deconstruct(GHDFCompound* self)
{
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    Result = GHDFCompound_Clear(self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = HashMap_Deconstruct(&self->_entryMetadata);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = HashMap_Deconstruct(&self->_entries);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Memory_Zero(self, sizeof(*self));
    return Error_CreateSuccess();
}

Error GHDFCompound_Clear(GHDFCompound* self)
{
    CollectionEnumerator* Enumerator = NULL;
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    Enumerator = ICollection_GetEnumerator(IMap_AsKeyCollection(HashMap_AsMap(&self->_entries)));
    if (Enumerator != NULL)
    {
        while (true)
        {
            bool HasNext = false;
            GHDFEntryID EntryID = GHDF_ENTRY_ID_INVALID;

            Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
            if (Result.Code != ErrorCode_Success)
            {
                break;
            }
            if (!HasNext)
            {
                Result = Error_CreateSuccess();
                break;
            }

            Result = CollectionEnumerator_NextByValue(Enumerator, &EntryID);
            if (Result.Code != ErrorCode_Success)
            {
                break;
            }

            Result = GHDFCompound_ReleaseEntryResources(self, EntryID);
            if (Result.Code != ErrorCode_Success)
            {
                break;
            }
        }

        CollectionEnumerator_Deconstruct(Enumerator);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    Result = IMap_Clear(HashMap_AsMap(&self->_entryMetadata));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return IMap_Clear(HashMap_AsMap(&self->_entries));
}

Error GHDFCompound_Remove(GHDFCompound* self, GHDFEntryID id)
{
    bool WasRemoved = false;
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (id == GHDF_ENTRY_ID_INVALID)
    {
        return CreateInvalidEntryIDError();
    }

    Result = GHDFCompound_ReleaseEntryResources(self, id);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = IMap_Remove(HashMap_AsMap(&self->_entryMetadata), &id, &WasRemoved);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return IMap_Remove(HashMap_AsMap(&self->_entries), &id, &WasRemoved);
}

Error GHDFCompound_Set(GHDFCompound* self, GHDFEntryID id, GHDFCompoundEntryType entryType, void* value)
{
    GHDFObjectValue ObjectValue;
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (id == GHDF_ENTRY_ID_INVALID)
    {
        return CreateInvalidEntryIDError();
    }

    Result = ValidateCompoundEntryType(entryType);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    if (entryType.IsArray)
    {
        if (value == NULL)
        {
            return CreateNullArgumentError(u8"value");
        }

        GHDFObjectValue_Zero(&ObjectValue);
        ObjectValue.Type = entryType.ValueType;
        ObjectValue.Value.Array = value;
        return GHDFCompound_SetEntryValue(self, id, entryType, ObjectValue, NULL);
    }

    Result = GHDFObjectValue_FromScalar(entryType.ValueType, value, &ObjectValue);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return GHDFCompound_SetEntryValue(self, id, entryType, ObjectValue, NULL);
}

Error GHDFCompound_Get(GHDFCompound* self, GHDFEntryID id, GHDFObjectValue* outEntry)
{
    GHDFEntryMetadata Metadata;
    UNUSED(Metadata);
    return GHDFCompound_GetEntryMetadata(self, id, outEntry, &Metadata);
}

Error GHDFCompound_GetOptional(GHDFCompound* self, GHDFEntryID id, GHDFObjectValue* outEntry, bool* outWasFound)
{
    GHDFEntryMetadata Metadata;
    Error Result = Error_CreateSuccess();

    if (outWasFound == NULL)
    {
        return CreateNullArgumentError(u8"outWasFound");
    }

    *outWasFound = false;
    Result = GHDFCompound_GetEntryMetadata(self, id, outEntry, &Metadata);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Error_CreateSuccess();
    }

    *outWasFound = true;
    return Error_CreateSuccess();
}

Error GHDFCompound_GetVerified(GHDFCompound* self,
    GHDFEntryID id,
    GHDFCompoundEntryType expectedType,
    GHDFObjectValue* outEntry)
{
    GHDFEntryMetadata Metadata;
    Error Result = ValidateCompoundEntryType(expectedType);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = GHDFCompound_GetEntryMetadata(self, id, outEntry, &Metadata);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((outEntry->Type != expectedType.ValueType) || (Metadata.IsArray != expectedType.IsArray))
    {
        return CreateTypeMismatchError(outEntry->Type, expectedType, Metadata.IsArray);
    }

    return Error_CreateSuccess();
}

Error GHDFCompound_GetOptionalVerified(GHDFCompound* self,
    GHDFEntryID id,
    GHDFCompoundEntryType expectedType,
    GHDFObjectValue* outEntry,
    bool* outWasFound)
{
    Error Result = GHDFCompound_GetOptional(self, id, outEntry, outWasFound);

    if ((Result.Code != ErrorCode_Success) || !(*outWasFound))
    {
        return Result;
    }

    return GHDFCompound_GetVerified(self, id, expectedType, outEntry);
}

size_t GHDFCompound_GetEntryCount(GHDFCompound* self)
{
    if (self == NULL)
    {
        return 0U;
    }

    return IMap_GetEntryCount(HashMap_AsMap(&self->_entries));
}

ICollection* GHDFCompound_AsEntryCollection(GHDFCompound* self)
{
    if (self == NULL)
    {
        return NULL;
    }

    return IMap_AsEntryCollection(HashMap_AsMap(&self->_entries));
}

ICollection* GHDFCompound_AsValueCollection(GHDFCompound* self)
{
    if (self == NULL)
    {
        return NULL;
    }

    return IMap_AsValueCollection(HashMap_AsMap(&self->_entries));
}

ICollection* GHDFCompound_AsKeyCollection(GHDFCompound* self)
{
    if (self == NULL)
    {
        return NULL;
    }

    return IMap_AsKeyCollection(HashMap_AsMap(&self->_entries));
}

Error GHDFArray_Construct1(GHDFArray* self, GHDFValueType elementType)
{
    Error Result = ValidateArrayElementType(elementType);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return GHDFArray_Initialize(self, elementType);
}

Error GHDFArray_Deconstruct(GHDFArray* self)
{
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    Result = GHDFArray_Clear(self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    ArrayList_Deconstruct(&self->_valueMetadata);
    ArrayList_Deconstruct(&self->_values);
    Memory_Zero(self, sizeof(*self));
    return Error_CreateSuccess();
}

Error GHDFArray_Clear(GHDFArray* self)
{
    size_t ElementCount = 0U;
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    ElementCount = IList_GetElementCount(&self->_values._list);
    for (size_t Index = 0; Index < ElementCount; Index++)
    {
        Result = GHDFArray_ReleaseElementResources(self, Index);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    Result = IList_Clear(&self->_valueMetadata._list);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = IList_Clear(&self->_values._list);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    if (self->_ownerPool != NULL)
    {
        self->_elementType = GHDFValueType_None;
    }
    return Error_CreateSuccess();
}

Error GHDFArray_RemoveAt(GHDFArray* self, size_t index)
{
    Error Result = GHDFArray_ReleaseElementResources(self, index);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = IList_RemoveAt(&self->_valueMetadata._list, index);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return IList_RemoveAt(&self->_values._list, index);
}

Error GHDFArray_Add(GHDFArray* self, void* element)
{
    return GHDFArray_Insert(self, IList_GetElementCount(&self->_values._list), element);
}

Error GHDFArray_Insert(GHDFArray* self, size_t index, void* element)
{
    GHDFObjectValue Value;
    Error Result = GHDFArray_CreateValue(self, element, &Value);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return GHDFArray_SetElementValue(self, index, Value, NULL, false);
}

Error GHDFArray_Replace(GHDFArray* self, size_t index, void* element)
{
    GHDFObjectValue Value;
    Error Result = GHDFArray_CreateValue(self, element, &Value);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return GHDFArray_SetElementValue(self, index, Value, NULL, true);
}

Error GHDFArray_Get(GHDFArray* self, size_t index, GHDFObjectValue* outValue)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outValue == NULL)
    {
        return CreateNullArgumentError(u8"outValue");
    }

    return IList_GetElement(&self->_values._list, index, outValue);
}

size_t GHDFArray_GetElementCount(GHDFArray* self)
{
    if (self == NULL)
    {
        return 0U;
    }

    return IList_GetElementCount(&self->_values._list);
}

GHDFValueType GHDFArray_GetElementType(GHDFArray* self)
{
    if (self == NULL)
    {
        return GHDFValueType_None;
    }

    return self->_elementType;
}

ICollection* GHDFArray_AsElementCollection(GHDFArray* self)
{
    if (self == NULL)
    {
        return NULL;
    }

    return IList_AsCollection(&self->_values._list);
}

Error GHDFObjectPool_Create(GHDFObjectPool** outPool)
{
    GHDFObjectPool* Pool = NULL;
    Error Result = Error_CreateSuccess();

    if (outPool == NULL)
    {
        return CreateNullArgumentError(u8"outPool");
    }

    *outPool = NULL;
    Pool = Memory_Allocate(sizeof(*Pool));
    Result = GHDFObjectPool_Construct(Pool);
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(Pool);
        return Result;
    }

    *outPool = Pool;
    return Error_CreateSuccess();
}

Error GHDFObjectPool_Deconstruct(GHDFObjectPool* self)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    ObjectPool_Deconstruct(&self->_compoundPool);
    ObjectPool_Deconstruct(&self->_arrayPool);
    ObjectPool_Deconstruct(&self->_stringPool);
    Memory_Free(self);
    return Error_CreateSuccess();
}

Error GHDFObjectPool_BorrowCompound(GHDFObjectPool* self, GHDFCompound** outCompound)
{
    GHDFCompound* Compound = NULL;
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outCompound == NULL)
    {
        return CreateNullArgumentError(u8"outCompound");
    }

    Result = ObjectPool_GetNewObject(&self->_compoundPool, (void**)&Compound);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Compound->_ownerPool = self;
    *outCompound = Compound;
    return Error_CreateSuccess();
}

Error GHDFObjectPool_BorrowArray(GHDFObjectPool* self, GHDFArray** outArray)
{
    GHDFArray* Array = NULL;
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outArray == NULL)
    {
        return CreateNullArgumentError(u8"outArray");
    }

    Result = ObjectPool_GetNewObject(&self->_arrayPool, (void**)&Array);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Array->_ownerPool = self;
    *outArray = Array;
    return Error_CreateSuccess();
}

Error GHDFObjectPool_BorrowString(GHDFObjectPool* self, GenericBuffer** outStringBuffer)
{
    GenericBuffer* StringBuffer = NULL;
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outStringBuffer == NULL)
    {
        return CreateNullArgumentError(u8"outStringBuffer");
    }

    Result = ObjectPool_GetNewObject(&self->_stringPool, (void**)&StringBuffer);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    *outStringBuffer = StringBuffer;
    return Error_CreateSuccess();
}

Error GHDFObjectPool_ReturnCompound(GHDFObjectPool* self, GHDFCompound* compound, bool includeNestedStructures)
{
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (compound == NULL)
    {
        return CreateNullArgumentError(u8"compound");
    }

    if (includeNestedStructures)
    {
        Result = GHDFObjectPool_ReturnNestedFromCompound(self, compound);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    return ObjectPool_DisposeObject(&self->_compoundPool, compound);
}

Error GHDFObjectPool_ReturnArray(GHDFObjectPool* self, GHDFArray* array, bool includeNestedStructures)
{
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (array == NULL)
    {
        return CreateNullArgumentError(u8"array");
    }

    if (includeNestedStructures)
    {
        Result = GHDFObjectPool_ReturnNestedFromArray(self, array);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    return ObjectPool_DisposeObject(&self->_arrayPool, array);
}

Error GHDFObjectPool_ReturnString(GHDFObjectPool* self, GenericBuffer* stringBuffer)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (stringBuffer == NULL)
    {
        return CreateNullArgumentError(u8"stringBuffer");
    }

    return ObjectPool_DisposeObject(&self->_stringPool, stringBuffer);
}

GHDFCompoundEntryType GHDF_CreateRegularType(GHDFValueType valueType)
{
    return (GHDFCompoundEntryType) {
        .ValueType = valueType,
        .IsArray = false,
    };
}

GHDFCompoundEntryType GHDF_CreateArrayType(GHDFValueType valueType)
{
    return (GHDFCompoundEntryType) {
        .ValueType = valueType,
        .IsArray = true,
    };
}
