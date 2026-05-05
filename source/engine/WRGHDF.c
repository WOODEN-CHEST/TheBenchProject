#include "WRGHDF.h"
#include "WRArrayList.h"
#include "WRBinaryIO.h"
#include "WRHash.h"
#include "WRHashMap.h"
#include "WRMap.h"
#include "WRObjectPool.h"
#include <inttypes.h>


// Macros.
#define GHDF_TYPE_ARRAY_BIT ((uint8_t)0x80U)
#define GHDF_VERSION ((uint64_t)1U)
#define GHDF_POOL_SECTION_CAPACITY ((size_t)16U)


// Types.
typedef enum GHDFBorrowKindEnum
{
    GHDFBorrowKind_None = 0,
    GHDFBorrowKind_Compound = 1,
    GHDFBorrowKind_Array = 2,
    GHDFBorrowKind_String = 3
} GHDFBorrowKind;

typedef struct GHDFBorrowTokenStruct
{
    GHDFBorrowKind Kind;
    GHDFObjectPool* OwnerPool;
} GHDFBorrowToken;

typedef struct GHDFStoredCompoundEntryStruct
{
    GHDFCompoundEntryType EntryType;
    GHDFObjectValue Value;
} GHDFStoredCompoundEntry;

typedef struct GHDFCompoundEntryEnumeratorStruct
{
    CollectionEnumerator Base;
    CollectionEnumerator* _innerEnumerator;
} GHDFCompoundEntryEnumerator;

typedef struct GHDFCompoundValueEnumeratorStruct
{
    CollectionEnumerator Base;
    CollectionEnumerator* _innerEnumerator;
} GHDFCompoundValueEnumerator;

typedef struct GHDFArrayElementEnumeratorStruct
{
    CollectionEnumerator Base;
    GHDFArray* _array;
    size_t _currentIndex;
} GHDFArrayElementEnumerator;

struct GHDFCompoundStruct
{
    HashMap _entries;
    GHDFBorrowToken _borrowToken;
    ICollection _entryCollection;
    ICollection _valueCollection;
};

struct GHDFArrayStruct
{
    ArrayList _values;
    GHDFValueType _elementType;
    GHDFBorrowToken _borrowToken;
    ICollection _elementCollection;
};

struct GHDFObjectPoolStruct
{
    ObjectPool _compoundPool;
    ObjectPool _arrayPool;
    ObjectPool _stringPool;
};


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

static Error CreateInvalidBorrowedResourceError(const unsigned char* resourceName)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"GHDF %s must be borrowed from a GHDF object pool.",
        resourceName);
}

static Error CreateInvalidStringBufferError(void)
{
    return Error_Construct1(ErrorCode_IllegalArgument,
        u8"GHDF string buffers must be UTF-8 byte buffers with a trailing null terminator.");
}

static Error CreateElementSizeMismatchError(const unsigned char* argumentName, size_t actualElementSize, size_t expectedElementSize)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"GHDF argument \"%s\" must use element size %zu, got %zu.",
        argumentName,
        expectedElementSize,
        actualElementSize);
}

static Error CreateInvalidPoolOwnershipError(const unsigned char* resourceName)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"The provided GHDF %s does not belong to the specified object pool.",
        resourceName);
}

static Error CreateIndexOutOfRangeError(size_t index, size_t elementCount)
{
    return Error_Construct3(ErrorCode_ArgumentOutOfRange,
        u8"GHDF array index %zu is out of range for an array with %zu elements.",
        index,
        elementCount);
}

static Error CreateElementRangeOutOfRangeError(size_t startIndex, size_t requestedCount, size_t actualCount)
{
    return Error_Construct3(ErrorCode_ArgumentOutOfRange,
        u8"GHDF array range [%zu, %zu) is out of range for an array with %zu elements.",
        startIndex,
        startIndex + requestedCount,
        actualCount);
}

static Error CreateEnumerationCompletedError(void)
{
    return Error_Construct1(ErrorCode_InvalidOperation,
        u8"The GHDF enumerator has already reached the end of the collection.");
}

static Error CreateReferenceEnumerationNotSupportedError(void)
{
    return Error_Construct1(ErrorCode_InvalidOperation,
        u8"This GHDF collection does not support returning elements by reference.");
}

static size_t GetStringByteCount(GenericBuffer* stringBuffer)
{
    if (stringBuffer == NULL)
    {
        return 0U;
    }
    if ((stringBuffer->_count > 0U) && (stringBuffer->_data[stringBuffer->_count - 1U] == 0U))
    {
        return stringBuffer->_count - 1U;
    }

    return stringBuffer->_count;
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

static size_t GHDFArray_GetStorageElementSize(GHDFValueType elementType)
{
    switch (elementType)
    {
        case GHDFValueType_UInt8:
            return sizeof(uint8_t);

        case GHDFValueType_Int8:
            return sizeof(int8_t);

        case GHDFValueType_Int16:
            return sizeof(int16_t);

        case GHDFValueType_UInt16:
            return sizeof(uint16_t);

        case GHDFValueType_Int32:
            return sizeof(int32_t);

        case GHDFValueType_UInt32:
            return sizeof(uint32_t);

        case GHDFValueType_Int64:
            return sizeof(int64_t);

        case GHDFValueType_UInt64:
            return sizeof(uint64_t);

        case GHDFValueType_Float:
            return sizeof(float);

        case GHDFValueType_Double:
            return sizeof(double);

        case GHDFValueType_Boolean:
            return sizeof(bool);

        case GHDFValueType_String:
            return sizeof(GenericBuffer*);

        case GHDFValueType_Compound:
            return sizeof(GHDFCompound*);

        case GHDFValueType_EncodedInteger:
            return sizeof(int64_t);

        case GHDFValueType_None:
        default:
            return 0U;
    }
}

static bool GHDFArray_IsRawByteCopySupported(GHDFValueType elementType)
{
    switch (elementType)
    {
        case GHDFValueType_UInt8:
        case GHDFValueType_Int8:
        case GHDFValueType_Int16:
        case GHDFValueType_UInt16:
        case GHDFValueType_Int32:
        case GHDFValueType_UInt32:
        case GHDFValueType_Int64:
        case GHDFValueType_UInt64:
        case GHDFValueType_Float:
        case GHDFValueType_Double:
        case GHDFValueType_Boolean:
        case GHDFValueType_EncodedInteger:
            return true;

        case GHDFValueType_String:
        case GHDFValueType_Compound:
        case GHDFValueType_None:
        default:
            return false;
    }
}

static Error CreateRawByteCopyUnsupportedError(GHDFValueType elementType)
{
    return Error_Construct3(ErrorCode_InvalidOperation,
        u8"GHDF arrays of element type %d do not support raw byte copying.",
        (int)elementType);
}

static Error GHDFCompoundEntryCollection_GetNextValue(MapEntryView entryView, GHDFCompoundEntry* outEntry)
{
    GHDFEntryID* Id = NULL;
    GHDFStoredCompoundEntry* StoredEntry = NULL;

    if (outEntry == NULL)
    {
        return CreateNullArgumentError(u8"outEntry");
    }

    Id = entryView._key;
    StoredEntry = entryView._value;
    if ((Id == NULL) || (StoredEntry == NULL))
    {
        return Error_Construct1(ErrorCode_InvalidState,
            u8"Encountered an invalid GHDF compound entry during iteration.");
    }

    outEntry->Id = *Id;
    outEntry->EntryType = StoredEntry->EntryType;
    outEntry->Value = StoredEntry->Value;
    return Error_CreateSuccess();
}

static Error GHDFCompoundValueCollection_GetNextValue(MapEntryView entryView, GHDFObjectValue* outValue)
{
    GHDFStoredCompoundEntry* StoredEntry = NULL;

    if (outValue == NULL)
    {
        return CreateNullArgumentError(u8"outValue");
    }

    StoredEntry = entryView._value;
    if (StoredEntry == NULL)
    {
        return Error_Construct1(ErrorCode_InvalidState,
            u8"Encountered an invalid GHDF compound value during iteration.");
    }

    *outValue = StoredEntry->Value;
    return Error_CreateSuccess();
}

static Error GHDFCompoundEntryEnumerator_HasNext(void* self, bool* outHasNext)
{
    GHDFCompoundEntryEnumerator* Enumerator = self;

    if (Enumerator == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outHasNext == NULL)
    {
        return CreateNullArgumentError(u8"outHasNext");
    }

    return CollectionEnumerator_HasNext(Enumerator->_innerEnumerator, outHasNext);
}

static Error GHDFCompoundEntryEnumerator_NextByValue(void* self, void* outEntryValue)
{
    GHDFCompoundEntryEnumerator* Enumerator = self;
    MapEntryView EntryView;
    Error Result = Error_CreateSuccess();

    if (Enumerator == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outEntryValue == NULL)
    {
        return CreateNullArgumentError(u8"outEntryValue");
    }

    Result = CollectionEnumerator_NextByValue(Enumerator->_innerEnumerator, &EntryView);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return GHDFCompoundEntryCollection_GetNextValue(EntryView, outEntryValue);
}

static Error GHDFCompoundEntryEnumerator_NextByReference(void* self, void** outPointer)
{
    (void)self;
    (void)outPointer;
    return CreateReferenceEnumerationNotSupportedError();
}

static void GHDFCompoundEntryEnumerator_Deconstruct(void* self)
{
    GHDFCompoundEntryEnumerator* Enumerator = self;

    if (Enumerator == NULL)
    {
        return;
    }
    if (Enumerator->_innerEnumerator != NULL)
    {
        CollectionEnumerator_Deconstruct(Enumerator->_innerEnumerator);
    }

    Memory_Free(Enumerator);
}

static CollectionEnumerator* GHDFCompoundEntryCollection_GetEnumerator(void* self)
{
    static const CollectionEnumeratorVTable EnumeratorVTable =
    {
        .Self = NULL,
        ._hasNext = GHDFCompoundEntryEnumerator_HasNext,
        ._nextByValue = GHDFCompoundEntryEnumerator_NextByValue,
        ._nextByReference = GHDFCompoundEntryEnumerator_NextByReference,
        ._deconstruct = GHDFCompoundEntryEnumerator_Deconstruct,
    };
    GHDFCompound* Compound = self;
    GHDFCompoundEntryEnumerator* Enumerator = NULL;
    ICollection* UnderlyingCollection = NULL;

    if (Compound == NULL)
    {
        return NULL;
    }

    UnderlyingCollection = IMap_AsEntryCollection(HashMap_AsMap(&Compound->_entries));
    Enumerator = Memory_Allocate(sizeof(*Enumerator));
    Enumerator->Base._singleElementSize = sizeof(GHDFCompoundEntry);
    Enumerator->Base._flags = EnumeratorFlags_None;
    Enumerator->Base._vtable = EnumeratorVTable;
    Enumerator->Base._vtable.Self = Enumerator;
    Enumerator->_innerEnumerator = ICollection_GetEnumerator(UnderlyingCollection);
    return &Enumerator->Base;
}

static Error GHDFCompoundValueEnumerator_HasNext(void* self, bool* outHasNext)
{
    GHDFCompoundValueEnumerator* Enumerator = self;

    if (Enumerator == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outHasNext == NULL)
    {
        return CreateNullArgumentError(u8"outHasNext");
    }

    return CollectionEnumerator_HasNext(Enumerator->_innerEnumerator, outHasNext);
}

static Error GHDFCompoundValueEnumerator_NextByValue(void* self, void* outEntryValue)
{
    GHDFCompoundValueEnumerator* Enumerator = self;
    MapEntryView EntryView;
    Error Result = Error_CreateSuccess();

    if (Enumerator == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outEntryValue == NULL)
    {
        return CreateNullArgumentError(u8"outEntryValue");
    }

    Result = CollectionEnumerator_NextByValue(Enumerator->_innerEnumerator, &EntryView);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return GHDFCompoundValueCollection_GetNextValue(EntryView, outEntryValue);
}

static Error GHDFCompoundValueEnumerator_NextByReference(void* self, void** outPointer)
{
    (void)self;
    (void)outPointer;
    return CreateReferenceEnumerationNotSupportedError();
}

static void GHDFCompoundValueEnumerator_Deconstruct(void* self)
{
    GHDFCompoundValueEnumerator* Enumerator = self;

    if (Enumerator == NULL)
    {
        return;
    }
    if (Enumerator->_innerEnumerator != NULL)
    {
        CollectionEnumerator_Deconstruct(Enumerator->_innerEnumerator);
    }

    Memory_Free(Enumerator);
}

static CollectionEnumerator* GHDFCompoundValueCollection_GetEnumerator(void* self)
{
    static const CollectionEnumeratorVTable EnumeratorVTable =
    {
        .Self = NULL,
        ._hasNext = GHDFCompoundValueEnumerator_HasNext,
        ._nextByValue = GHDFCompoundValueEnumerator_NextByValue,
        ._nextByReference = GHDFCompoundValueEnumerator_NextByReference,
        ._deconstruct = GHDFCompoundValueEnumerator_Deconstruct,
    };
    GHDFCompound* Compound = self;
    GHDFCompoundValueEnumerator* Enumerator = NULL;
    ICollection* UnderlyingCollection = NULL;

    if (Compound == NULL)
    {
        return NULL;
    }

    UnderlyingCollection = IMap_AsEntryCollection(HashMap_AsMap(&Compound->_entries));
    Enumerator = Memory_Allocate(sizeof(*Enumerator));
    Enumerator->Base._singleElementSize = sizeof(GHDFObjectValue);
    Enumerator->Base._flags = EnumeratorFlags_None;
    Enumerator->Base._vtable = EnumeratorVTable;
    Enumerator->Base._vtable.Self = Enumerator;
    Enumerator->_innerEnumerator = ICollection_GetEnumerator(UnderlyingCollection);
    return &Enumerator->Base;
}

static Error GHDFArray_GetValueAt(GHDFArray* self, size_t index, GHDFObjectValue* outValue);

static Error GHDFArrayElementEnumerator_HasNext(void* self, bool* outHasNext)
{
    GHDFArrayElementEnumerator* Enumerator = self;

    if (Enumerator == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outHasNext == NULL)
    {
        return CreateNullArgumentError(u8"outHasNext");
    }

    *outHasNext = (Enumerator->_currentIndex < GHDFArray_GetElementCount(Enumerator->_array));
    return Error_CreateSuccess();
}

static Error GHDFArrayElementEnumerator_NextByValue(void* self, void* outEntryValue)
{
    GHDFArrayElementEnumerator* Enumerator = self;
    GHDFArrayIndexedValue* IndexedValue = outEntryValue;
    Error Result = Error_CreateSuccess();

    if (Enumerator == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (IndexedValue == NULL)
    {
        return CreateNullArgumentError(u8"outEntryValue");
    }
    if (Enumerator->_currentIndex >= GHDFArray_GetElementCount(Enumerator->_array))
    {
        return CreateEnumerationCompletedError();
    }

    IndexedValue->Index = Enumerator->_currentIndex;
    Result = GHDFArray_GetValueAt(Enumerator->_array, Enumerator->_currentIndex, &IndexedValue->Value);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Enumerator->_currentIndex++;
    return Error_CreateSuccess();
}

static Error GHDFArrayElementEnumerator_NextByReference(void* self, void** outPointer)
{
    (void)self;
    (void)outPointer;
    return CreateReferenceEnumerationNotSupportedError();
}

static void GHDFArrayElementEnumerator_Deconstruct(void* self)
{
    GHDFArrayElementEnumerator* Enumerator = self;

    if (Enumerator == NULL)
    {
        return;
    }

    Memory_Free(Enumerator);
}

static CollectionEnumerator* GHDFArrayElementCollection_GetEnumerator(void* self)
{
    static const CollectionEnumeratorVTable EnumeratorVTable =
    {
        .Self = NULL,
        ._hasNext = GHDFArrayElementEnumerator_HasNext,
        ._nextByValue = GHDFArrayElementEnumerator_NextByValue,
        ._nextByReference = GHDFArrayElementEnumerator_NextByReference,
        ._deconstruct = GHDFArrayElementEnumerator_Deconstruct,
    };
    GHDFArray* Array = self;
    GHDFArrayElementEnumerator* Enumerator = NULL;

    if (Array == NULL)
    {
        return NULL;
    }

    Enumerator = Memory_Allocate(sizeof(*Enumerator));
    Enumerator->Base._singleElementSize = sizeof(GHDFArrayIndexedValue);
    Enumerator->Base._flags = EnumeratorFlags_None;
    Enumerator->Base._vtable = EnumeratorVTable;
    Enumerator->Base._vtable.Self = Enumerator;
    Enumerator->_array = Array;
    Enumerator->_currentIndex = 0U;
    return &Enumerator->Base;
}

static Error GHDFCompound_Initialize(GHDFCompound* self)
{
    static const ICollectionVtable EntryCollectionVTable =
    {
        .Self = NULL,
        ._getEnumerator = GHDFCompoundEntryCollection_GetEnumerator,
    };
    static const ICollectionVtable ValueCollectionVTable =
    {
        .Self = NULL,
        ._getEnumerator = GHDFCompoundValueCollection_GetEnumerator,
    };
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    Memory_Zero(self, sizeof(*self));
    Result = GHDFHashMap_Construct(&self->_entries, sizeof(GHDFStoredCompoundEntry));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    self->_borrowToken.Kind = GHDFBorrowKind_Compound;
    self->_borrowToken.OwnerPool = NULL;
    self->_entryCollection._vtable = EntryCollectionVTable;
    self->_entryCollection._vtable.Self = self;
    self->_valueCollection._vtable = ValueCollectionVTable;
    self->_valueCollection._vtable.Self = self;
    return Error_CreateSuccess();
}

static Error GHDFArray_Initialize(GHDFArray* self)
{
    static const ICollectionVtable ElementCollectionVTable =
    {
        .Self = NULL,
        ._getEnumerator = GHDFArrayElementCollection_GetEnumerator,
    };

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    Memory_Zero(self, sizeof(*self));
    ArrayList_Construct1(&self->_values, sizeof(unsigned char));
    self->_elementType = GHDFValueType_None;
    self->_borrowToken.Kind = GHDFBorrowKind_Array;
    self->_borrowToken.OwnerPool = NULL;
    self->_elementCollection._vtable = ElementCollectionVTable;
    self->_elementCollection._vtable.Self = self;
    return Error_CreateSuccess();
}

static Error GHDFArray_PrepareForElementType(GHDFArray* self, GHDFValueType elementType)
{
    size_t ElementSize = 0U;
    Error Result = ValidateArrayElementType(elementType);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    ElementSize = GHDFArray_GetStorageElementSize(elementType);
    if (ElementSize == 0U)
    {
        return CreateInvalidTypeError(elementType);
    }
    if (IList_GetElementSize(&self->_values._list) != ElementSize)
    {
        ArrayList_Deconstruct(&self->_values);
        ArrayList_Construct1(&self->_values, ElementSize);
    }

    self->_elementType = elementType;
    return Error_CreateSuccess();
}

static bool GHDFBorrowToken_IsOwnedByPool(const GHDFBorrowToken* token, GHDFBorrowKind expectedKind, GHDFObjectPool* pool)
{
    return (token != NULL)
        && (token->Kind == expectedKind)
        && (token->OwnerPool == pool);
}

static bool GHDFStringBuffer_IsBorrowed(GenericBuffer* stringBuffer, GHDFObjectPool** outOwnerPool)
{
    GHDFBorrowToken* Token = NULL;

    if (outOwnerPool != NULL)
    {
        *outOwnerPool = NULL;
    }
    if (stringBuffer == NULL)
    {
        return false;
    }

    Token = stringBuffer->_userData;
    if ((Token == NULL) || (Token->Kind != GHDFBorrowKind_String) || (Token->OwnerPool == NULL))
    {
        return false;
    }
    if (outOwnerPool != NULL)
    {
        *outOwnerPool = Token->OwnerPool;
    }

    return true;
}

static bool GHDFCompound_IsBorrowed(GHDFCompound* compound, GHDFObjectPool** outOwnerPool)
{
    if (outOwnerPool != NULL)
    {
        *outOwnerPool = NULL;
    }
    if ((compound == NULL) || (compound->_borrowToken.Kind != GHDFBorrowKind_Compound) || (compound->_borrowToken.OwnerPool == NULL))
    {
        return false;
    }
    if (outOwnerPool != NULL)
    {
        *outOwnerPool = compound->_borrowToken.OwnerPool;
    }

    return true;
}

static bool GHDFArray_IsBorrowed(GHDFArray* array, GHDFObjectPool** outOwnerPool)
{
    if (outOwnerPool != NULL)
    {
        *outOwnerPool = NULL;
    }
    if ((array == NULL) || (array->_borrowToken.Kind != GHDFBorrowKind_Array) || (array->_borrowToken.OwnerPool == NULL))
    {
        return false;
    }
    if (outOwnerPool != NULL)
    {
        *outOwnerPool = array->_borrowToken.OwnerPool;
    }

    return true;
}

static bool GHDFStringBuffer_IsValidForStorage(GenericBuffer* stringBuffer)
{
    if (stringBuffer == NULL)
    {
        return false;
    }
    if (stringBuffer->_elementSize != sizeof(unsigned char))
    {
        return false;
    }
    if (stringBuffer->_count == 0U)
    {
        return false;
    }

    return stringBuffer->_data[stringBuffer->_count - 1U] == 0U;
}

static Error GHDFValidateBorrowedString(GenericBuffer* stringBuffer)
{
    if (!GHDFStringBuffer_IsBorrowed(stringBuffer, NULL))
    {
        return CreateInvalidBorrowedResourceError(u8"string");
    }
    if (!GHDFStringBuffer_IsValidForStorage(stringBuffer))
    {
        return CreateInvalidStringBufferError();
    }

    return Error_CreateSuccess();
}

static Error GHDFValidateBorrowedCompound(GHDFCompound* compound)
{
    if (!GHDFCompound_IsBorrowed(compound, NULL))
    {
        return CreateInvalidBorrowedResourceError(u8"compound");
    }

    return Error_CreateSuccess();
}

static Error GHDFValidateBorrowedArray(GHDFArray* array, GHDFValueType expectedType)
{
    if (!GHDFArray_IsBorrowed(array, NULL))
    {
        return CreateInvalidBorrowedResourceError(u8"array");
    }
    if (array->_elementType != expectedType)
    {
        return CreateTypeMismatchError(array->_elementType, GHDF_CreateArrayType(expectedType), true);
    }

    return Error_CreateSuccess();
}

static Error GHDFStringBuffer_ConstructObject(void* object, void* userData)
{
    GenericBuffer* Buffer = object;
    GHDFObjectPool* Pool = userData;
    GHDFBorrowToken* Token = NULL;

    if (Buffer == NULL)
    {
        return CreateNullArgumentError(u8"object");
    }

    GenericBuffer_AllocateVariable(Buffer, 0, sizeof(unsigned char));
    Token = Memory_Allocate(sizeof(*Token));
    Token->Kind = GHDFBorrowKind_String;
    Token->OwnerPool = Pool;
    Buffer->_userData = Token;
    return Error_CreateSuccess();
}

static Error GHDFStringBuffer_ResetObject(void* object, void* userData)
{
    GenericBuffer* Buffer = object;
    GHDFBorrowToken* Token = NULL;
    GHDFObjectPool* Pool = userData;

    if (Buffer == NULL)
    {
        return CreateNullArgumentError(u8"object");
    }
    if (!GenericBuffer_Clear(Buffer))
    {
        return Error_Construct1(ErrorCode_InvalidOperation,
            u8"Could not clear a GHDF string buffer.");
    }

    Token = Buffer->_userData;
    if (Token != NULL)
    {
        Token->Kind = GHDFBorrowKind_String;
        Token->OwnerPool = Pool;
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

    Memory_Free(Buffer->_userData);
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

    Compound->_borrowToken.OwnerPool = Pool;
    return Error_CreateSuccess();
}

static Error GHDFArrayPool_ConstructObject(void* object, void* userData)
{
    GHDFArray* Array = object;
    GHDFObjectPool* Pool = userData;
    Error Result = GHDFArray_Initialize(Array);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Array->_borrowToken.OwnerPool = Pool;
    return Error_CreateSuccess();
}

static Error GHDFObjectValue_Release(GHDFCompoundEntryType entryType, const GHDFObjectValue* value);

static Error GHDFCompound_ClearInternal(GHDFCompound* self)
{
    CollectionEnumerator* Enumerator = NULL;
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    Enumerator = ICollection_GetEnumerator(IMap_AsEntryCollection(HashMap_AsMap(&self->_entries)));
    if (Enumerator != NULL)
    {
        while (true)
        {
            bool HasNext = false;
            MapEntryView EntryView;
            GHDFStoredCompoundEntry* StoredEntry = NULL;

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

            StoredEntry = EntryView._value;
            if (StoredEntry == NULL)
            {
                Result = Error_Construct1(ErrorCode_InvalidState,
                    u8"Encountered an invalid GHDF compound entry while clearing.");
                break;
            }

            Result = GHDFObjectValue_Release(StoredEntry->EntryType, &StoredEntry->Value);
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

    return IMap_Clear(HashMap_AsMap(&self->_entries));
}

static Error GHDFCompoundPool_ResetObject(void* object, void* userData)
{
    GHDFCompound* Compound = object;
    GHDFObjectPool* Pool = userData;
    Error Result = GHDFCompound_ClearInternal(Compound);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Compound->_borrowToken.Kind = GHDFBorrowKind_Compound;
    Compound->_borrowToken.OwnerPool = Pool;
    return Error_CreateSuccess();
}

static Error GHDFCompoundPool_DeconstructObject(void* object, void* userData)
{
    GHDFCompound* Compound = object;
    (void)userData;
    Error Result = Error_CreateSuccess();

    if (Compound == NULL)
    {
        return CreateNullArgumentError(u8"object");
    }

    Result = GHDFCompound_ClearInternal(Compound);
    if (Result.Code == ErrorCode_Success)
    {
        Result = HashMap_Deconstruct(&Compound->_entries);
    }

    Memory_Zero(Compound, sizeof(*Compound));
    return Result;
}

static Error GHDFArray_ClearInternal(GHDFArray* self)
{
    size_t ElementCount = 0U;
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    ElementCount = IList_GetElementCount(&self->_values._list);
    for (size_t Index = 0U; Index < ElementCount; Index++)
    {
        GHDFObjectValue Value;

        Result = GHDFArray_GetValueAt(self, Index, &Value);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        Result = GHDFObjectValue_Release(GHDF_CreateRegularType(self->_elementType), &Value);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    return IList_Clear(&self->_values._list);
}

static Error GHDFArrayPool_ResetObject(void* object, void* userData)
{
    GHDFArray* Array = object;
    GHDFObjectPool* Pool = userData;
    Error Result = GHDFArray_ClearInternal(Array);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Array->_borrowToken.Kind = GHDFBorrowKind_Array;
    Array->_borrowToken.OwnerPool = Pool;
    Array->_elementType = GHDFValueType_None;
    return Error_CreateSuccess();
}

static Error GHDFArrayPool_DeconstructObject(void* object, void* userData)
{
    GHDFArray* Array = object;
    (void)userData;
    Error Result = Error_CreateSuccess();

    if (Array == NULL)
    {
        return CreateNullArgumentError(u8"object");
    }

    Result = GHDFArray_ClearInternal(Array);
    ArrayList_Deconstruct(&Array->_values);
    Memory_Zero(Array, sizeof(*Array));
    return Result;
}

static Error GHDFObjectValue_Release(GHDFCompoundEntryType entryType, const GHDFObjectValue* value)
{
    GHDFObjectPool* OwnerPool = NULL;

    if (value == NULL)
    {
        return CreateNullArgumentError(u8"value");
    }

    if (entryType.IsArray)
    {
        if (value->Value.Array == NULL)
        {
            return Error_CreateSuccess();
        }
        if (!GHDFArray_IsBorrowed(value->Value.Array, &OwnerPool))
        {
            return CreateInvalidBorrowedResourceError(u8"array");
        }

        return GHDFObjectPool_ReturnArray(OwnerPool, value->Value.Array, true);
    }

    switch (entryType.ValueType)
    {
        case GHDFValueType_String:
            if (value->Value.String == NULL)
            {
                return Error_CreateSuccess();
            }
            if (!GHDFStringBuffer_IsBorrowed(value->Value.String, &OwnerPool))
            {
                return CreateInvalidBorrowedResourceError(u8"string");
            }

            return GHDFObjectPool_ReturnString(OwnerPool, value->Value.String);

        case GHDFValueType_Compound:
            if (value->Value.Compound == NULL)
            {
                return Error_CreateSuccess();
            }
            if (!GHDFCompound_IsBorrowed(value->Value.Compound, &OwnerPool))
            {
                return CreateInvalidBorrowedResourceError(u8"compound");
            }

            return GHDFObjectPool_ReturnCompound(OwnerPool, value->Value.Compound, true);

        default:
            return Error_CreateSuccess();
    }
}

static Error GHDFValidateValueAgainstEntryType(GHDFCompoundEntryType entryType, const GHDFObjectValue* value)
{
    Error Result = ValidateCompoundEntryType(entryType);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (value == NULL)
    {
        return CreateNullArgumentError(u8"value");
    }
    if (value->Type != entryType.ValueType)
    {
        return CreateTypeMismatchError(value->Type, entryType, false);
    }

    if (entryType.IsArray)
    {
        return GHDFValidateBorrowedArray(value->Value.Array, entryType.ValueType);
    }

    switch (entryType.ValueType)
    {
        case GHDFValueType_String:
            return GHDFValidateBorrowedString(value->Value.String);

        case GHDFValueType_Compound:
            return GHDFValidateBorrowedCompound(value->Value.Compound);

        default:
            return Error_CreateSuccess();
    }
}

static Error GHDFValidateValueAgainstArrayType(GHDFArray* self, const GHDFObjectValue* value)
{
    GHDFCompoundEntryType ExpectedType;

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (self->_elementType == GHDFValueType_None)
    {
        return CreateInvalidTypeError(self->_elementType);
    }

    ExpectedType = GHDF_CreateRegularType(self->_elementType);
    return GHDFValidateValueAgainstEntryType(ExpectedType, value);
}

static const void* GHDFObjectValue_GetStoragePointer(const GHDFObjectValue* value)
{
    switch (value->Type)
    {
        case GHDFValueType_UInt8:
            return &value->Value.UInt8;

        case GHDFValueType_Int8:
            return &value->Value.Int8;

        case GHDFValueType_Int16:
            return &value->Value.Int16;

        case GHDFValueType_UInt16:
            return &value->Value.UInt16;

        case GHDFValueType_Int32:
            return &value->Value.Int32;

        case GHDFValueType_UInt32:
            return &value->Value.UInt32;

        case GHDFValueType_Int64:
            return &value->Value.Int64;

        case GHDFValueType_UInt64:
            return &value->Value.UInt64;

        case GHDFValueType_Float:
            return &value->Value.Float;

        case GHDFValueType_Double:
            return &value->Value.Double;

        case GHDFValueType_Boolean:
            return &value->Value.Boolean;

        case GHDFValueType_String:
            return &value->Value.String;

        case GHDFValueType_Compound:
            return &value->Value.Compound;

        case GHDFValueType_EncodedInteger:
            return &value->Value.EncodedInteger;

        case GHDFValueType_None:
        default:
            return NULL;
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

static Error GHDF_WriteStringValue(BinaryIOStream* stream, GenericBuffer* stringBuffer)
{
    size_t ByteCount = 0U;
    Error Result = GHDFValidateBorrowedString(stringBuffer);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    ByteCount = GetStringByteCount(stringBuffer);
    Result = BinaryIOStream_WriteEncodedUInt64(stream, (uint64_t)ByteCount);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return GHDFWriteExactBytes(stream, stringBuffer->_data, ByteCount);
}

static Error GHDF_ReadOwnedStringValue(BinaryIOStream* stream,
    GHDFObjectPool* objectPool,
    GenericBuffer** outString)
{
    uint64_t Length64 = 0U;
    size_t Length = 0U;
    GenericBuffer* StringBuffer = NULL;
    Error Result = Error_CreateSuccess();

    if (outString == NULL)
    {
        return CreateNullArgumentError(u8"outString");
    }

    *outString = NULL;
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

    *outString = StringBuffer;
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
    GHDFObjectValue* outValue);

static Error GHDF_WriteArray(BinaryIOStream* stream, GHDFArray* array);

static Error GHDF_WriteCompoundBody(BinaryIOStream* stream, const GHDFCompound* compound);

static Error GHDF_ReadCompoundBody(BinaryIOStream* stream, GHDFObjectPool* objectPool, GHDFCompound* compound);

static Error GHDF_ReadArray(BinaryIOStream* stream,
    GHDFObjectPool* objectPool,
    GHDFValueType elementType,
    GHDFArray** outArray);

static Error GHDF_ReadScalarValue(BinaryIOStream* stream,
    GHDFValueType valueType,
    GHDFObjectPool* objectPool,
    GHDFObjectValue* outValue)
{
    if (outValue == NULL)
    {
        return CreateNullArgumentError(u8"outValue");
    }

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
            return GHDF_ReadOwnedStringValue(stream, objectPool, &outValue->Value.String);

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
    GHDFObjectValue* outValue)
{
    Error Result = Error_CreateSuccess();

    if (outValue == NULL)
    {
        return CreateNullArgumentError(u8"outValue");
    }

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

    return GHDF_ReadScalarValue(stream, entryType.ValueType, objectPool, outValue);
}

static Error GHDF_WriteArray(BinaryIOStream* stream, GHDFArray* array)
{
    size_t ElementCount = 0U;
    Error Result = Error_CreateSuccess();

    if (array == NULL)
    {
        return CreateNullArgumentError(u8"array");
    }

    ElementCount = IList_GetElementCount(&array->_values._list);
    if (array->_elementType == GHDFValueType_None)
    {
        return CreateInvalidTypeError(array->_elementType);
    }

    Result = BinaryIOStream_WriteEncodedUInt64(stream, (uint64_t)ElementCount);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    for (size_t Index = 0U; Index < ElementCount; Index++)
    {
        GHDFObjectValue Value;

        Result = GHDFArray_GetValueAt(array, Index, &Value);
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

    Result = GHDFObjectPool_BorrowArray(objectPool, elementType, &Array);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    for (size_t Index = 0U; Index < ElementCount; Index++)
    {
        GHDFObjectValue Value;

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
            Result = GHDF_ReadScalarValue(stream, elementType, objectPool, &Value);
        }
        if (Result.Code != ErrorCode_Success)
        {
            (void)GHDFObjectValue_Release(GHDF_CreateRegularType(elementType), &Value);
            (void)GHDFObjectPool_ReturnArray(objectPool, Array, true);
            return Result;
        }

        Result = GHDFArray_AddValue(Array, &Value);
        if (Result.Code != ErrorCode_Success)
        {
            (void)GHDFObjectValue_Release(GHDF_CreateRegularType(elementType), &Value);
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
        GHDFEntryID* EntryID = NULL;
        GHDFStoredCompoundEntry* StoredEntry = NULL;

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

        EntryID = EntryView._key;
        StoredEntry = EntryView._value;
        if ((EntryID == NULL) || (StoredEntry == NULL))
        {
            Result = Error_Construct1(ErrorCode_InvalidState,
                u8"Encountered an invalid GHDF compound entry during serialization.");
            break;
        }

        Result = BinaryIOStream_WriteEncodedUInt64(stream, *EntryID);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }

        Result = BinaryIOStream_WriteUInt8(stream, GHDF_ComposeTypeByte(StoredEntry->EntryType));
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }

        Result = GHDF_WriteEntryValue(stream, StoredEntry->EntryType, StoredEntry->Value);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }
    }

    CollectionEnumerator_Deconstruct(Enumerator);
    return Result;
}

static Error GHDF_ReadCompoundBody(BinaryIOStream* stream, GHDFObjectPool* objectPool, GHDFCompound* compound)
{
    uint64_t EntryCount64 = 0U;
    size_t EntryCount = 0U;
    Error Result = Error_CreateSuccess();

    if (compound == NULL)
    {
        return CreateNullArgumentError(u8"compound");
    }

    Result = BinaryIOStream_ReadEncodedUInt64(stream, &EntryCount64);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = ConvertUInt64ToSize(EntryCount64, u8"compound", &EntryCount);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    for (size_t Index = 0U; Index < EntryCount; Index++)
    {
        uint64_t EntryID = 0U;
        uint8_t TypeByte = 0U;
        GHDFCompoundEntryType EntryType;
        GHDFObjectValue Value;

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

        Result = GHDF_ReadEntryValue(stream, objectPool, EntryType, &Value);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        Result = GHDFCompound_SetValue(compound, EntryID, EntryType, &Value);
        if (Result.Code != ErrorCode_Success)
        {
            (void)GHDFObjectValue_Release(EntryType, &Value);
            return Result;
        }
    }

    return Error_CreateSuccess();
}

static Error GHDFArray_GetValueAt(GHDFArray* self, size_t index, GHDFObjectValue* outValue)
{
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outValue == NULL)
    {
        return CreateNullArgumentError(u8"outValue");
    }

    switch (self->_elementType)
    {
        case GHDFValueType_UInt8:
        {
            uint8_t Value = 0U;
            Result = IList_GetElement(&self->_values._list, index, &Value);
            if (Result.Code == ErrorCode_Success)
            {
                *outValue = GHDFObjectValue_CreateUInt8(Value);
            }
            return Result;
        }

        case GHDFValueType_Int8:
        {
            int8_t Value = 0;
            Result = IList_GetElement(&self->_values._list, index, &Value);
            if (Result.Code == ErrorCode_Success)
            {
                *outValue = GHDFObjectValue_CreateInt8(Value);
            }
            return Result;
        }

        case GHDFValueType_Int16:
        {
            int16_t Value = 0;
            Result = IList_GetElement(&self->_values._list, index, &Value);
            if (Result.Code == ErrorCode_Success)
            {
                *outValue = GHDFObjectValue_CreateInt16(Value);
            }
            return Result;
        }

        case GHDFValueType_UInt16:
        {
            uint16_t Value = 0U;
            Result = IList_GetElement(&self->_values._list, index, &Value);
            if (Result.Code == ErrorCode_Success)
            {
                *outValue = GHDFObjectValue_CreateUInt16(Value);
            }
            return Result;
        }

        case GHDFValueType_Int32:
        {
            int32_t Value = 0;
            Result = IList_GetElement(&self->_values._list, index, &Value);
            if (Result.Code == ErrorCode_Success)
            {
                *outValue = GHDFObjectValue_CreateInt32(Value);
            }
            return Result;
        }

        case GHDFValueType_UInt32:
        {
            uint32_t Value = 0U;
            Result = IList_GetElement(&self->_values._list, index, &Value);
            if (Result.Code == ErrorCode_Success)
            {
                *outValue = GHDFObjectValue_CreateUInt32(Value);
            }
            return Result;
        }

        case GHDFValueType_Int64:
        {
            int64_t Value = 0;
            Result = IList_GetElement(&self->_values._list, index, &Value);
            if (Result.Code == ErrorCode_Success)
            {
                *outValue = GHDFObjectValue_CreateInt64(Value);
            }
            return Result;
        }

        case GHDFValueType_UInt64:
        {
            uint64_t Value = 0U;
            Result = IList_GetElement(&self->_values._list, index, &Value);
            if (Result.Code == ErrorCode_Success)
            {
                *outValue = GHDFObjectValue_CreateUInt64(Value);
            }
            return Result;
        }

        case GHDFValueType_Float:
        {
            float Value = 0.0f;
            Result = IList_GetElement(&self->_values._list, index, &Value);
            if (Result.Code == ErrorCode_Success)
            {
                *outValue = GHDFObjectValue_CreateFloat(Value);
            }
            return Result;
        }

        case GHDFValueType_Double:
        {
            double Value = 0.0;
            Result = IList_GetElement(&self->_values._list, index, &Value);
            if (Result.Code == ErrorCode_Success)
            {
                *outValue = GHDFObjectValue_CreateDouble(Value);
            }
            return Result;
        }

        case GHDFValueType_Boolean:
        {
            bool Value = false;
            Result = IList_GetElement(&self->_values._list, index, &Value);
            if (Result.Code == ErrorCode_Success)
            {
                *outValue = GHDFObjectValue_CreateBoolean(Value);
            }
            return Result;
        }

        case GHDFValueType_String:
        {
            GenericBuffer* Value = NULL;
            Result = IList_GetElement(&self->_values._list, index, &Value);
            if (Result.Code == ErrorCode_Success)
            {
                *outValue = GHDFObjectValue_CreateString(Value);
            }
            return Result;
        }

        case GHDFValueType_Compound:
        {
            GHDFCompound* Value = NULL;
            Result = IList_GetElement(&self->_values._list, index, &Value);
            if (Result.Code == ErrorCode_Success)
            {
                *outValue = GHDFObjectValue_CreateCompound(Value);
            }
            return Result;
        }

        case GHDFValueType_EncodedInteger:
        {
            int64_t Value = 0;
            Result = IList_GetElement(&self->_values._list, index, &Value);
            if (Result.Code == ErrorCode_Success)
            {
                *outValue = GHDFObjectValue_CreateEncodedInteger(Value);
            }
            return Result;
        }

        case GHDFValueType_None:
        default:
            return CreateInvalidTypeError(self->_elementType);
    }
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
        Error CleanupResult = ObjectPool_Deconstruct(&self->_compoundPool);
        if (CleanupResult.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            return CleanupResult;
        }

        return Result;
    }

    Result = ObjectPool_Construct2(&self->_stringPool,
        sizeof(GenericBuffer),
        GHDF_POOL_SECTION_CAPACITY,
        StringLifecycle,
        self);
    if (Result.Code != ErrorCode_Success)
    {
        Error CleanupResult = ObjectPool_Deconstruct(&self->_arrayPool);
        if (CleanupResult.Code == ErrorCode_Success)
        {
            CleanupResult = ObjectPool_Deconstruct(&self->_compoundPool);
        }
        else
        {
            Error DeconstructCompoundResult = ObjectPool_Deconstruct(&self->_compoundPool);
            if (DeconstructCompoundResult.Code != ErrorCode_Success)
            {
                Error_Deconstruct(&DeconstructCompoundResult);
            }
        }
        if (CleanupResult.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            return CleanupResult;
        }

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

    Error CleanupResult = BinaryIOStream_Deconstruct(&BinaryStream);
    if (CleanupResult.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return CleanupResult;
    }

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

    Error CleanupResult = BinaryIOStream_Deconstruct(&BinaryStream);
    if (Result.Code != ErrorCode_Success)
    {
        if (Root != NULL)
        {
            (void)GHDFObjectPool_ReturnCompound(objectPool, Root, true);
        }
        if (CleanupResult.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            return CleanupResult;
        }

        return Result;
    }
    if (CleanupResult.Code != ErrorCode_Success)
    {
        return CleanupResult;
    }

    *outRoot = Root;
    return Error_CreateSuccess();
}

Error GHDFCompound_Clear(GHDFCompound* self)
{
    return GHDFCompound_ClearInternal(self);
}

Error GHDFCompound_Remove(GHDFCompound* self, GHDFEntryID id)
{
    GHDFStoredCompoundEntry StoredEntry;
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

    Result = IMap_GetElement(HashMap_AsMap(&self->_entries), &id, &StoredEntry);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return CreateEntryNotFoundError(id);
    }

    Result = GHDFObjectValue_Release(StoredEntry.EntryType, &StoredEntry.Value);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return IMap_Remove(HashMap_AsMap(&self->_entries), &id, &WasRemoved);
}

Error GHDFCompound_SetValue(GHDFCompound* self,
    GHDFEntryID id,
    GHDFCompoundEntryType entryType,
    const GHDFObjectValue* value)
{
    GHDFStoredCompoundEntry StoredEntry;
    GHDFStoredCompoundEntry ExistingEntry;
    bool HasExistingEntry = false;
    bool WasAdded = false;
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (id == GHDF_ENTRY_ID_INVALID)
    {
        return CreateInvalidEntryIDError();
    }

    Result = GHDFValidateValueAgainstEntryType(entryType, value);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = IMap_GetElement(HashMap_AsMap(&self->_entries), &id, &ExistingEntry);
    if (Result.Code == ErrorCode_Success)
    {
        HasExistingEntry = true;
    }
    else
    {
        Error_Deconstruct(&Result);
    }

    StoredEntry.EntryType = entryType;
    StoredEntry.Value = *value;
    Result = IMap_Add(HashMap_AsMap(&self->_entries), &id, &StoredEntry, &WasAdded);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (HasExistingEntry)
    {
        return GHDFObjectValue_Release(ExistingEntry.EntryType, &ExistingEntry.Value);
    }

    return Error_CreateSuccess();
}

Error GHDFCompound_Get(GHDFCompound* self, GHDFEntryID id, GHDFObjectValue* outEntry)
{
    GHDFStoredCompoundEntry StoredEntry;
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outEntry == NULL)
    {
        return CreateNullArgumentError(u8"outEntry");
    }

    Result = IMap_GetElement(HashMap_AsMap(&self->_entries), &id, &StoredEntry);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return CreateEntryNotFoundError(id);
    }

    *outEntry = StoredEntry.Value;
    return Error_CreateSuccess();
}

Error GHDFCompound_GetOptional(GHDFCompound* self, GHDFEntryID id, GHDFObjectValue* outEntry, bool* outWasFound)
{
    GHDFStoredCompoundEntry StoredEntry;
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outEntry == NULL)
    {
        return CreateNullArgumentError(u8"outEntry");
    }
    if (outWasFound == NULL)
    {
        return CreateNullArgumentError(u8"outWasFound");
    }

    *outWasFound = false;
    Result = IMap_GetElement(HashMap_AsMap(&self->_entries), &id, &StoredEntry);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Error_CreateSuccess();
    }

    *outWasFound = true;
    *outEntry = StoredEntry.Value;
    return Error_CreateSuccess();
}

Error GHDFCompound_GetVerified(GHDFCompound* self,
    GHDFEntryID id,
    GHDFCompoundEntryType expectedType,
    GHDFObjectValue* outEntry)
{
    GHDFStoredCompoundEntry StoredEntry;
    Error Result = ValidateCompoundEntryType(expectedType);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outEntry == NULL)
    {
        return CreateNullArgumentError(u8"outEntry");
    }

    Result = IMap_GetElement(HashMap_AsMap(&self->_entries), &id, &StoredEntry);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return CreateEntryNotFoundError(id);
    }
    if ((StoredEntry.EntryType.ValueType != expectedType.ValueType) || (StoredEntry.EntryType.IsArray != expectedType.IsArray))
    {
        return CreateTypeMismatchError(StoredEntry.EntryType.ValueType, expectedType, StoredEntry.EntryType.IsArray);
    }

    *outEntry = StoredEntry.Value;
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

ICollection* GHDFCompound_GetEntryCollection(GHDFCompound* self)
{
    return (self == NULL) ? NULL : &self->_entryCollection;
}

ICollection* GHDFCompound_GetValueCollection(GHDFCompound* self)
{
    return (self == NULL) ? NULL : &self->_valueCollection;
}

ICollection* GHDFCompound_GetKeyCollection(GHDFCompound* self)
{
    return (self == NULL) ? NULL : IMap_AsKeyCollection(HashMap_AsMap(&self->_entries));
}

Error GHDFArray_Clear(GHDFArray* self)
{
    return GHDFArray_ClearInternal(self);
}

Error GHDFArray_RemoveAt(GHDFArray* self, size_t index)
{
    GHDFObjectValue Value;
    Error Result = GHDFArray_GetValueAt(self, index, &Value);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = GHDFObjectValue_Release(GHDF_CreateRegularType(self->_elementType), &Value);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return IList_RemoveAt(&self->_values._list, index);
}

Error GHDFArray_AddValue(GHDFArray* self, const GHDFObjectValue* value)
{
    return GHDFArray_InsertValue(self, IList_GetElementCount(&self->_values._list), value);
}

Error GHDFArray_InsertValue(GHDFArray* self, size_t index, const GHDFObjectValue* value)
{
    const void* StoragePointer = NULL;
    Error Result = GHDFValidateValueAgainstArrayType(self, value);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    StoragePointer = GHDFObjectValue_GetStoragePointer(value);
    if (StoragePointer == NULL)
    {
        return CreateInvalidArrayValueError();
    }

    return IList_Insert(&self->_values._list, index, (void*)StoragePointer);
}

Error GHDFArray_ReplaceValue(GHDFArray* self, size_t index, const GHDFObjectValue* value)
{
    GHDFObjectValue ExistingValue;
    const void* StoragePointer = NULL;
    Error Result = GHDFValidateValueAgainstArrayType(self, value);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = GHDFArray_GetValueAt(self, index, &ExistingValue);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    StoragePointer = GHDFObjectValue_GetStoragePointer(value);
    if (StoragePointer == NULL)
    {
        return CreateInvalidArrayValueError();
    }

    Result = IList_Replace(&self->_values._list, index, (void*)StoragePointer);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return GHDFObjectValue_Release(GHDF_CreateRegularType(self->_elementType), &ExistingValue);
}

Error GHDFArray_Get(GHDFArray* self, size_t index, GHDFObjectValue* outValue)
{
    return GHDFArray_GetValueAt(self, index, outValue);
}

Error GHDFArray_CopyRawBytes(GHDFArray* self,
    size_t startIndex,
    size_t elementCount,
    GenericBuffer* destination)
{
    size_t TotalElementCount = 0U;
    size_t StorageElementSize = 0U;
    void* StartPointer = NULL;
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (destination == NULL)
    {
        return CreateNullArgumentError(u8"destination");
    }
    if (self->_elementType == GHDFValueType_None)
    {
        return CreateInvalidTypeError(self->_elementType);
    }
    if (!GHDFArray_IsRawByteCopySupported(self->_elementType))
    {
        return CreateRawByteCopyUnsupportedError(self->_elementType);
    }

    TotalElementCount = IList_GetElementCount(&self->_values._list);
    StorageElementSize = IList_GetElementSize(&self->_values._list);
    if (destination->_elementSize != StorageElementSize)
    {
        return CreateElementSizeMismatchError(u8"destination", destination->_elementSize, StorageElementSize);
    }
    if (startIndex > TotalElementCount)
    {
        return CreateIndexOutOfRangeError(startIndex, TotalElementCount);
    }
    if (elementCount > (TotalElementCount - startIndex))
    {
        return CreateElementRangeOutOfRangeError(startIndex, elementCount, TotalElementCount);
    }
    if (elementCount == 0U)
    {
        return Error_CreateSuccess();
    }

    if (elementCount > (SIZE_MAX / StorageElementSize))
    {
        return Error_Construct1(ErrorCode_BufferTooLarge,
            u8"GHDF array raw byte copy exceeds addressable size.");
    }
    Result = IList_GetPointerToElement(&self->_values._list, startIndex, &StartPointer);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!GenericBuffer_AddLastRange(destination, StartPointer, elementCount))
    {
        return Error_Construct1(ErrorCode_BufferTooSmall,
            u8"Could not append GHDF array elements to the destination buffer.");
    }

    return Error_CreateSuccess();
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

ICollection* GHDFArray_GetElementCollection(GHDFArray* self)
{
    return (self == NULL) ? NULL : &self->_elementCollection;
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
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    Result = ObjectPool_Deconstruct(&self->_stringPool);
    if (Result.Code == ErrorCode_Success)
    {
        Result = ObjectPool_Deconstruct(&self->_arrayPool);
    }
    else
    {
        Error DeconstructArrayResult = ObjectPool_Deconstruct(&self->_arrayPool);
        if (DeconstructArrayResult.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&DeconstructArrayResult);
        }
    }

    if (Result.Code == ErrorCode_Success)
    {
        Result = ObjectPool_Deconstruct(&self->_compoundPool);
    }
    else
    {
        Error DeconstructCompoundResult = ObjectPool_Deconstruct(&self->_compoundPool);
        if (DeconstructCompoundResult.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&DeconstructCompoundResult);
        }
    }

    Memory_Free(self);
    return Result;
}

Error GHDFObjectPool_BorrowCompound(GHDFObjectPool* self, GHDFCompound** outCompound)
{
    void* Object = NULL;
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outCompound == NULL)
    {
        return CreateNullArgumentError(u8"outCompound");
    }

    *outCompound = NULL;
    Result = ObjectPool_GetNewObject(&self->_compoundPool, &Object);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    *outCompound = Object;
    return Error_CreateSuccess();
}

Error GHDFObjectPool_BorrowArray(GHDFObjectPool* self, GHDFValueType elementType, GHDFArray** outArray)
{
    void* Object = NULL;
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

    *outArray = NULL;
    Result = ValidateArrayElementType(elementType);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ObjectPool_GetNewObject(&self->_arrayPool, &Object);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Array = Object;
    Result = GHDFArray_PrepareForElementType(Array, elementType);
    if (Result.Code != ErrorCode_Success)
    {
        (void)ObjectPool_DisposeObject(&self->_arrayPool, Array);
        return Result;
    }

    *outArray = Array;
    return Error_CreateSuccess();
}

Error GHDFObjectPool_BorrowString(GHDFObjectPool* self, GenericBuffer** outStringBuffer)
{
    void* Object = NULL;
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outStringBuffer == NULL)
    {
        return CreateNullArgumentError(u8"outStringBuffer");
    }

    *outStringBuffer = NULL;
    Result = ObjectPool_GetNewObject(&self->_stringPool, &Object);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    *outStringBuffer = Object;
    return Error_CreateSuccess();
}

Error GHDFObjectPool_ReturnCompound(GHDFObjectPool* self, GHDFCompound* compound, bool includeNestedStructures)
{
    (void)includeNestedStructures;

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (compound == NULL)
    {
        return Error_CreateSuccess();
    }
    if (!GHDFBorrowToken_IsOwnedByPool(&compound->_borrowToken, GHDFBorrowKind_Compound, self))
    {
        return CreateInvalidPoolOwnershipError(u8"compound");
    }

    return ObjectPool_DisposeObject(&self->_compoundPool, compound);
}

Error GHDFObjectPool_ReturnArray(GHDFObjectPool* self, GHDFArray* array, bool includeNestedStructures)
{
    (void)includeNestedStructures;

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (array == NULL)
    {
        return Error_CreateSuccess();
    }
    if (!GHDFBorrowToken_IsOwnedByPool(&array->_borrowToken, GHDFBorrowKind_Array, self))
    {
        return CreateInvalidPoolOwnershipError(u8"array");
    }

    return ObjectPool_DisposeObject(&self->_arrayPool, array);
}

Error GHDFObjectPool_ReturnString(GHDFObjectPool* self, GenericBuffer* stringBuffer)
{
    GHDFBorrowToken* Token = NULL;

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (stringBuffer == NULL)
    {
        return Error_CreateSuccess();
    }

    Token = stringBuffer->_userData;
    if (!GHDFStringBuffer_IsBorrowed(stringBuffer, NULL) || (Token->OwnerPool != self))
    {
        return CreateInvalidPoolOwnershipError(u8"string");
    }

    return ObjectPool_DisposeObject(&self->_stringPool, stringBuffer);
}

GHDFCompoundEntryType GHDF_CreateRegularType(GHDFValueType valueType)
{
    return (GHDFCompoundEntryType)
    {
        .ValueType = valueType,
        .IsArray = false,
    };
}

GHDFCompoundEntryType GHDF_CreateArrayType(GHDFValueType valueType)
{
    return (GHDFCompoundEntryType)
    {
        .ValueType = valueType,
        .IsArray = true,
    };
}
