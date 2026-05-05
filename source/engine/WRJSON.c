#include "WRJSON.h"
#include "WRArrayList.h"
#include "WRHash.h"
#include "WRHashMap.h"
#include "WRMap.h"
#include "WRList.h"
#include "WRChar.h"
#include "WRNumber.h"
#include "WRString.h"
#include <limits.h>
#include <math.h>


// Macros.
#define JSON_INDENT_SIZE ((size_t)4)
#define JSON_ENTRY_COLLECTION_KIND_ENTRY ((uint8_t)0)
#define JSON_INDEXED_COLLECTION_KIND_INDEXED ((uint8_t)1)


// Types.
typedef struct JSONParserStruct
{
    JSONObjectPool* Pool;
    const unsigned char* Data;
    size_t Length;
    size_t Index;
} JSONParser;

typedef struct JSONCompoundEntryEnumeratorStruct
{
    CollectionEnumerator Base;
    CollectionEnumerator* _inner;
    JSONCompoundEntry _currentEntry;
} JSONCompoundEntryEnumerator;

typedef struct JSONArrayIndexedEnumeratorStruct
{
    CollectionEnumerator Base;
    JSONArray* _array;
    size_t _currentIndex;
    JSONArrayIndexedValue _currentValue;
} JSONArrayIndexedEnumerator;

typedef struct JSONCompoundStruct
{
    JSONObjectPool* _pool;
    HashMap _elements;
    ICollection _entryCollection;
} JSONCompound;

typedef struct JSONArrayStruct
{
    JSONObjectPool* _pool;
    ArrayList _elements;
    ICollection _indexedCollection;
} JSONArray;

typedef struct JSONObjectPoolStruct
{
    ArrayList _allCompounds;
    ArrayList _availableCompounds;
    ArrayList _allArrays;
    ArrayList _availableArrays;
    ArrayList _allStrings;
    ArrayList _availableStrings;
    ArrayList _allKeys;
} JSONObjectPool;


// Fields.
static const unsigned char JSON_BYTE_QUOTE = u8'"';
static const unsigned char JSON_BYTE_BACKSLASH = u8'\\';
static const unsigned char JSON_BYTE_SLASH = u8'/';
static const unsigned char JSON_BYTE_BACKSPACE = u8'\b';
static const unsigned char JSON_BYTE_FORM_FEED = u8'\f';
static const unsigned char JSON_BYTE_NEWLINE = u8'\n';
static const unsigned char JSON_BYTE_CARRIAGE_RETURN = u8'\r';
static const unsigned char JSON_BYTE_TAB = u8'\t';
static const unsigned char JSON_BYTE_SPACE = u8' ';
static const unsigned char JSON_BYTE_OPEN_BRACE = u8'{';
static const unsigned char JSON_BYTE_CLOSE_BRACE = u8'}';
static const unsigned char JSON_BYTE_OPEN_BRACKET = u8'[';
static const unsigned char JSON_BYTE_CLOSE_BRACKET = u8']';
static const unsigned char JSON_BYTE_COLON = u8':';
static const unsigned char JSON_BYTE_COMMA = u8',';
static const unsigned char JSON_BYTE_MINUS = u8'-';
static const unsigned char JSON_BYTE_PLUS = u8'+';
static const unsigned char JSON_BYTE_PERIOD = u8'.';
static const unsigned char JSON_BYTE_ZERO = u8'0';
static const unsigned char JSON_BYTE_NINE = u8'9';
static const unsigned char JSON_BYTE_UPPER_A = u8'A';
static const unsigned char JSON_BYTE_UPPER_F = u8'F';
static const unsigned char JSON_BYTE_LOWER_A = u8'a';
static const unsigned char JSON_BYTE_LOWER_F = u8'f';
static const unsigned char JSON_BYTE_UPPER_E = u8'E';
static const unsigned char JSON_BYTE_LOWER_E = u8'e';

static const unsigned char* const JSON_LITERAL_TRUE = u8"true";
static const unsigned char* const JSON_LITERAL_FALSE = u8"false";
static const unsigned char* const JSON_LITERAL_NULL = u8"null";
static const unsigned char* const JSON_LITERAL_ESCAPE_QUOTE = u8"\\\"";
static const unsigned char* const JSON_LITERAL_ESCAPE_BACKSLASH = u8"\\\\";
static const unsigned char* const JSON_LITERAL_ESCAPE_BACKSPACE = u8"\\b";
static const unsigned char* const JSON_LITERAL_ESCAPE_FORM_FEED = u8"\\f";
static const unsigned char* const JSON_LITERAL_ESCAPE_NEWLINE = u8"\\n";
static const unsigned char* const JSON_LITERAL_ESCAPE_CARRIAGE_RETURN = u8"\\r";
static const unsigned char* const JSON_LITERAL_ESCAPE_TAB = u8"\\t";
static const unsigned char* const JSON_LITERAL_ESCAPE_UNICODE_PREFIX = u8"\\u";


// Static functions.
static Error CreateNullArgumentError(const unsigned char* argumentName)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"JSON argument \"%s\" must not be null.",
        argumentName);
}

static Error CreateByteBufferTypeError(const unsigned char* argumentName, size_t elementSize)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"JSON argument \"%s\" must be a byte buffer, got element size %zu.",
        argumentName,
        elementSize);
}

static Error CreateInvalidTypeError(JSONValueType type)
{
    return Error_Construct3(ErrorCode_InvalidOperation,
        u8"Unsupported JSON value type %d.",
        (int)type);
}

static Error CreateNotFoundError(const unsigned char* targetName)
{
    return Error_Construct3(ErrorCode_InvalidOperation,
        u8"The requested JSON %s was not found.",
        targetName);
}

static Error CreateTypeMismatchError(JSONValueType expectedType, JSONValueType actualType)
{
    return Error_Construct3(ErrorCode_InvalidOperation,
        u8"Expected JSON value type %d but got %d.",
        (int)expectedType,
        (int)actualType);
}

static Error CreatePoolMismatchError(void)
{
    return Error_Construct1(ErrorCode_InvalidOperation,
        u8"The JSON value does not belong to the specified object pool.");
}

static Error CreateCapacityError(const unsigned char* operationName)
{
    return Error_Construct3(ErrorCode_BufferTooSmall,
        u8"Could not %s because the destination buffer could not grow enough.",
        operationName);
}

static Error CreateInvalidJSONError(size_t index, const unsigned char* message)
{
    return Error_Construct3(ErrorCode_InvalidJSON,
        u8"Invalid JSON at byte index %zu: %s.",
        index,
        message);
}

static Error CreateSerializeError(const unsigned char* message)
{
    return Error_Construct3(ErrorCode_Serialize,
        u8"Could not serialize JSON: %s.",
        message);
}

static Error CreateDeserializeError(const unsigned char* message)
{
    return Error_Construct3(ErrorCode_Deserialize,
        u8"Could not deserialize JSON: %s.",
        message);
}

static size_t GetCStringLength(const unsigned char* text)
{
    size_t Length = 0;

    if (text == NULL)
    {
        return 0;
    }

    while (text[Length] != 0)
    {
        Length++;
    }

    return Length;
}

static bool BytesEqual(const unsigned char* a, size_t aLength, const unsigned char* b, size_t bLength)
{
    if (aLength != bLength)
    {
        return false;
    }
    if (aLength == 0)
    {
        return true;
    }

    return Memory_IsEqual(a, b, aLength);
}

static bool CStringEquals(const unsigned char* a, const unsigned char* b)
{
    size_t ALength = GetCStringLength(a);
    size_t BLength = GetCStringLength(b);

    return BytesEqual(a, ALength, b, BLength);
}

static bool TryAddSize(size_t a, size_t b, size_t* outSum)
{
    if (outSum == NULL)
    {
        return false;
    }
    if (b > (SIZE_MAX - a))
    {
        *outSum = 0;
        return false;
    }

    *outSum = a + b;
    return true;
}

static Error ValidateByteBuffer(GenericBuffer* buffer, const unsigned char* argumentName)
{
    if (buffer == NULL)
    {
        return CreateNullArgumentError(argumentName);
    }
    if (buffer->_elementSize != sizeof(unsigned char))
    {
        return CreateByteBufferTypeError(argumentName, buffer->_elementSize);
    }

    return Error_CreateSuccess();
}

static Error AppendByte(GenericBuffer* destination, unsigned char byte, const unsigned char* operationName)
{
    if (!GenericBuffer_AppendByte(destination, byte))
    {
        return CreateCapacityError(operationName);
    }

    return Error_CreateSuccess();
}

static Error AppendBytes(GenericBuffer* destination,
    const unsigned char* bytes,
    size_t byteCount,
    const unsigned char* operationName)
{
    for (size_t Index = 0; Index < byteCount; Index++)
    {
        Error Result = AppendByte(destination, bytes[Index], operationName);

        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    return Error_CreateSuccess();
}

static Error AppendCString(GenericBuffer* destination,
    const unsigned char* text,
    const unsigned char* operationName)
{
    return AppendBytes(destination, text, GetCStringLength(text), operationName);
}

static Error EnsureNullTerminatedCopy(const unsigned char* source,
    size_t byteCount,
    unsigned char** outString)
{
    unsigned char* NewString = NULL;

    if (outString == NULL)
    {
        return CreateNullArgumentError(u8"outString");
    }

    *outString = NULL;
    if ((byteCount > 0) && (source == NULL))
    {
        return CreateNullArgumentError(u8"source");
    }
    if (byteCount >= SIZE_MAX)
    {
        return CreateDeserializeError(u8"string size exceeds the supported range");
    }

    NewString = Memory_Allocate(byteCount + 1);
    if (NewString == NULL)
    {
        return CreateCapacityError(u8"allocate string storage");
    }

    if (byteCount > 0)
    {
        Memory_Copy(source, NewString, byteCount);
    }
    NewString[byteCount] = 0;
    *outString = NewString;
    return Error_CreateSuccess();
}

static bool IsJSONStringKeySupported(const unsigned char* bytes, size_t byteCount)
{
    for (size_t Index = 0; Index < byteCount; Index++)
    {
        if (bytes[Index] == 0)
        {
            return false;
        }
    }

    return true;
}

static bool IsWhitespaceByte(unsigned char byte)
{
    return (byte == JSON_BYTE_SPACE)
        || (byte == JSON_BYTE_TAB)
        || (byte == JSON_BYTE_NEWLINE)
        || (byte == JSON_BYTE_CARRIAGE_RETURN);
}

static bool IsDigitByte(unsigned char byte)
{
    return (byte >= JSON_BYTE_ZERO) && (byte <= JSON_BYTE_NINE);
}

static bool IsHexDigitByte(unsigned char byte)
{
    return IsDigitByte(byte)
        || ((byte >= JSON_BYTE_UPPER_A) && (byte <= JSON_BYTE_UPPER_F))
        || ((byte >= JSON_BYTE_LOWER_A) && (byte <= JSON_BYTE_LOWER_F));
}

static uint16_t GetHexDigitValue(unsigned char byte)
{
    if (IsDigitByte(byte))
    {
        return (uint16_t)(byte - JSON_BYTE_ZERO);
    }
    if ((byte >= JSON_BYTE_UPPER_A) && (byte <= JSON_BYTE_UPPER_F))
    {
        return (uint16_t)(10 + (byte - JSON_BYTE_UPPER_A));
    }

    return (uint16_t)(10 + (byte - JSON_BYTE_LOWER_A));
}

static bool IsHighSurrogate(uint16_t value)
{
    return (value >= UINT16_C(0xD800)) && (value <= UINT16_C(0xDBFF));
}

static bool IsLowSurrogate(uint16_t value)
{
    return (value >= UINT16_C(0xDC00)) && (value <= UINT16_C(0xDFFF));
}

static CodePoint DecodeSurrogatePair(uint16_t highSurrogate, uint16_t lowSurrogate)
{
    uint32_t HighPart = (uint32_t)(highSurrogate - UINT16_C(0xD800));
    uint32_t LowPart = (uint32_t)(lowSurrogate - UINT16_C(0xDC00));

    return (CodePoint)(UINT32_C(0x10000) + ((HighPart << 10) | LowPart));
}

static HashCode JSONKey_Hash(IMap* map, const void* key, void* userData)
{
    const unsigned char* const* KeyPointer = key;

    UNUSED(map);
    UNUSED(userData);
    if ((KeyPointer == NULL) || (*KeyPointer == NULL))
    {
        return 0;
    }

    return Hash_String(*KeyPointer);
}

static bool JSONKey_AreEqual(IMap* map, const void* key1, const void* key2, void* userData)
{
    const unsigned char* const* KeyPointer1 = key1;
    const unsigned char* const* KeyPointer2 = key2;

    UNUSED(map);
    UNUSED(userData);
    if ((KeyPointer1 == NULL) || (KeyPointer2 == NULL))
    {
        return false;
    }

    return CStringEquals(*KeyPointer1, *KeyPointer2);
}

static void InitializeCompoundCollection(JSONCompound* self);

static void InitializeArrayCollection(JSONArray* self);

static Error JSONObjectPool_RegisterKey(JSONObjectPool* self, unsigned char* keyCopy)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (keyCopy == NULL)
    {
        return CreateNullArgumentError(u8"keyCopy");
    }
    if (IList_AddLast(&self->_allKeys._list, &keyCopy).Code != ErrorCode_Success)
    {
        return CreateCapacityError(u8"register JSON object key");
    }

    return Error_CreateSuccess();
}

static Error JSONObjectPool_CreateCompoundStorage(JSONObjectPool* self, JSONCompound** outCompound)
{
    JSONCompound* Compound = NULL;
    HashMapConstructOptions Options;
    Error Result = Error_CreateSuccess();

    if (outCompound == NULL)
    {
        return CreateNullArgumentError(u8"outCompound");
    }

    *outCompound = NULL;
    Compound = Memory_Allocate(sizeof(*Compound));
    if (Compound == NULL)
    {
        return CreateCapacityError(u8"allocate JSON compound");
    }

    Compound->_pool = self;
    Options = HashMapConstructOptions_CreateDefault(sizeof(unsigned char*),
        sizeof(JSONObjectValue),
        JSONKey_Hash);
    Options.KeyComparator = JSONKey_AreEqual;
    Result = HashMap_Construct1(&Compound->_elements, Options);
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(Compound);
        return Result;
    }

    InitializeCompoundCollection(Compound);
    *outCompound = Compound;
    return Error_CreateSuccess();
}

static Error JSONObjectPool_CreateArrayStorage(JSONObjectPool* self, JSONArray** outArray)
{
    JSONArray* Array = NULL;

    if (outArray == NULL)
    {
        return CreateNullArgumentError(u8"outArray");
    }

    *outArray = NULL;
    Array = Memory_Allocate(sizeof(*Array));
    if (Array == NULL)
    {
        return CreateCapacityError(u8"allocate JSON array");
    }

    Array->_pool = self;
    ArrayList_Construct1(&Array->_elements, sizeof(JSONObjectValue));
    InitializeArrayCollection(Array);
    *outArray = Array;
    return Error_CreateSuccess();
}

static Error JSONObjectPool_CreateStringStorage(JSONObjectPool* self, GenericBuffer** outStringBuffer)
{
    GenericBuffer* StringBuffer = NULL;

    UNUSED(self);
    if (outStringBuffer == NULL)
    {
        return CreateNullArgumentError(u8"outStringBuffer");
    }

    *outStringBuffer = NULL;
    StringBuffer = Memory_Allocate(sizeof(*StringBuffer));
    if (StringBuffer == NULL)
    {
        return CreateCapacityError(u8"allocate JSON string");
    }

    GenericBuffer_AllocateVariable(StringBuffer, 0, sizeof(unsigned char));
    StringBuffer->_userData = self;
    *outStringBuffer = StringBuffer;
    return Error_CreateSuccess();
}

static Error ValidatePoolValueOwner(JSONObjectPool* self, JSONObjectValue* value)
{
    JSONObjectPool* OwnerPool = NULL;

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (value == NULL)
    {
        return CreateNullArgumentError(u8"value");
    }

    switch (value->Type)
    {
        case JSONValueType_String:
            if (value->Value.String != NULL)
            {
                OwnerPool = value->Value.String->_userData;
            }
            break;
        case JSONValueType_Compound:
            if (value->Value.Compound != NULL)
            {
                OwnerPool = value->Value.Compound->_pool;
            }
            break;
        case JSONValueType_Array:
            if (value->Value.Array != NULL)
            {
                OwnerPool = value->Value.Array->_pool;
            }
            break;
        default:
            return Error_CreateSuccess();
    }

    if (OwnerPool != self)
    {
        return CreatePoolMismatchError();
    }

    return Error_CreateSuccess();
}

static Error JSONCompound_GetOptionalInternal(JSONCompound* self,
    const unsigned char* key,
    JSONObjectValue* outValue,
    bool* outWasFound)
{
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (key == NULL)
    {
        return CreateNullArgumentError(u8"key");
    }
    if (outValue == NULL)
    {
        return CreateNullArgumentError(u8"outValue");
    }
    if (outWasFound == NULL)
    {
        return CreateNullArgumentError(u8"outWasFound");
    }

    Result = IMap_GetElement(HashMap_AsMap(&self->_elements), &key, outValue);
    if (Result.Code == ErrorCode_Success)
    {
        *outWasFound = true;
        return Error_CreateSuccess();
    }
    if (Result.Code == ErrorCode_InvalidOperation)
    {
        *outWasFound = false;
        *outValue = (JSONObjectValue){ .Type = JSONValueType_None };
        Error_Deconstruct(&Result);
        return Error_CreateSuccess();
    }

    return Result;
}

static Error JSONArray_GetOptionalInternal(JSONArray* self,
    size_t index,
    JSONObjectValue* outValue,
    bool* outWasFound)
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
    if (outWasFound == NULL)
    {
        return CreateNullArgumentError(u8"outWasFound");
    }

    Result = IList_GetElement(&self->_elements._list, index, outValue);
    if (Result.Code == ErrorCode_Success)
    {
        *outWasFound = true;
        return Error_CreateSuccess();
    }
    if (Result.Code == ErrorCode_IndexOutOfBounds)
    {
        *outWasFound = false;
        *outValue = (JSONObjectValue){ .Type = JSONValueType_None };
        Error_Deconstruct(&Result);
        return Error_CreateSuccess();
    }

    return Result;
}

static Error JSONCompound_SetOwnedKey(JSONCompound* self,
    const unsigned char* keyBytes,
    size_t keyByteCount,
    JSONObjectValue* value)
{
    const unsigned char* LookupKey = NULL;
    bool WasFound = false;
    JSONObjectValue ExistingValue;
    Error Result = Error_CreateSuccess();
    unsigned char* KeyCopy = NULL;
    bool WasAdded = false;

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (keyBytes == NULL)
    {
        return CreateNullArgumentError(u8"keyBytes");
    }
    if (value == NULL)
    {
        return CreateNullArgumentError(u8"value");
    }
    if (!IsJSONStringKeySupported(keyBytes, keyByteCount))
    {
        return CreateInvalidJSONError(0, u8"object keys containing U+0000 are not supported by the current API");
    }

    Result = EnsureNullTerminatedCopy(keyBytes, keyByteCount, &KeyCopy);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    LookupKey = KeyCopy;
    Result = JSONCompound_GetOptionalInternal(self, LookupKey, &ExistingValue, &WasFound);
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(KeyCopy);
        return Result;
    }

    if (WasFound)
    {
        Error ReturnResult = JSONObjectPool_ReturnValue(self->_pool, &ExistingValue);

        if (ReturnResult.Code != ErrorCode_Success)
        {
            Memory_Free(KeyCopy);
            return ReturnResult;
        }

        {
            JSONObjectValue* ExistingValuePointer = NULL;
            void* ExistingValuePointerVoid = NULL;

            Result = IMap_GetPointerToElement(HashMap_AsMap(&self->_elements), &LookupKey, &ExistingValuePointerVoid);
            if (Result.Code != ErrorCode_Success)
            {
                Memory_Free(KeyCopy);
                return Result;
            }

            ExistingValuePointer = ExistingValuePointerVoid;
            *ExistingValuePointer = *value;
        }

        Memory_Free(KeyCopy);
        return Error_CreateSuccess();
    }

    Result = JSONObjectPool_RegisterKey(self->_pool, KeyCopy);
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(KeyCopy);
        return Result;
    }

    Result = IMap_Add(HashMap_AsMap(&self->_elements), &LookupKey, value, &WasAdded);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!WasAdded)
    {
        return CreateSerializeError(u8"could not add a new JSON compound key");
    }

    return Error_CreateSuccess();
}

static Error JSONCompoundEntryEnumerator_HasNext(void* self, bool* outHasNext)
{
    JSONCompoundEntryEnumerator* EnumeratorSelf = self;

    if (EnumeratorSelf == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outHasNext == NULL)
    {
        return CreateNullArgumentError(u8"outHasNext");
    }

    return CollectionEnumerator_HasNext(EnumeratorSelf->_inner, outHasNext);
}

static Error JSONCompoundEntryEnumerator_NextByValue(void* self, void* outEntryValue)
{
    JSONCompoundEntryEnumerator* EnumeratorSelf = self;
    MapEntryView EntryView;
    JSONCompoundEntry* Entry = outEntryValue;
    Error Result = Error_CreateSuccess();

    if (EnumeratorSelf == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (Entry == NULL)
    {
        return CreateNullArgumentError(u8"outEntryValue");
    }

    Result = CollectionEnumerator_NextByValue(EnumeratorSelf->_inner, &EntryView);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Entry->Key = *(const unsigned char**)EntryView._key;
    Entry->Value = *(JSONObjectValue*)EntryView._value;
    EnumeratorSelf->_currentEntry = *Entry;
    return Error_CreateSuccess();
}

static Error JSONCompoundEntryEnumerator_NextByReference(void* self, void** outPointer)
{
    JSONCompoundEntryEnumerator* EnumeratorSelf = self;
    void* InnerPointer = NULL;
    MapEntryView* EntryView = NULL;
    Error Result = Error_CreateSuccess();

    if (EnumeratorSelf == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outPointer == NULL)
    {
        return CreateNullArgumentError(u8"outPointer");
    }

    Result = CollectionEnumerator_NextByReference(EnumeratorSelf->_inner, &InnerPointer);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    EntryView = InnerPointer;
    EnumeratorSelf->_currentEntry.Key = *(const unsigned char**)EntryView->_key;
    EnumeratorSelf->_currentEntry.Value = *(JSONObjectValue*)EntryView->_value;
    *outPointer = &EnumeratorSelf->_currentEntry;
    return Error_CreateSuccess();
}

static void JSONCompoundEntryEnumerator_Deconstruct(void* self)
{
    JSONCompoundEntryEnumerator* EnumeratorSelf = self;

    if (EnumeratorSelf == NULL)
    {
        return;
    }

    if (EnumeratorSelf->_inner != NULL)
    {
        CollectionEnumerator_Deconstruct(EnumeratorSelf->_inner);
    }
    Memory_Free(EnumeratorSelf);
}

static CollectionEnumerator* JSONCompound_GetEntryEnumerator(void* self)
{
    static const CollectionEnumeratorVTable EnumeratorTemplate =
    {
        .Self = NULL,
        ._hasNext = JSONCompoundEntryEnumerator_HasNext,
        ._nextByValue = JSONCompoundEntryEnumerator_NextByValue,
        ._nextByReference = JSONCompoundEntryEnumerator_NextByReference,
        ._deconstruct = JSONCompoundEntryEnumerator_Deconstruct,
    };
    JSONCompound* CompoundSelf = self;
    JSONCompoundEntryEnumerator* Enumerator = NULL;

    if (CompoundSelf == NULL)
    {
        return NULL;
    }

    Enumerator = Memory_Allocate(sizeof(*Enumerator));
    if (Enumerator == NULL)
    {
        return NULL;
    }

    Enumerator->_inner = ICollection_GetEnumerator(IMap_AsEntryCollection(HashMap_AsMap(&CompoundSelf->_elements)));
    if (Enumerator->_inner == NULL)
    {
        Memory_Free(Enumerator);
        return NULL;
    }

    Enumerator->Base._singleElementSize = sizeof(JSONCompoundEntry);
    Enumerator->Base._flags = EnumeratorFlags_CanReturnByReference;
    Enumerator->Base._vtable = EnumeratorTemplate;
    Enumerator->Base._vtable.Self = Enumerator;
    Enumerator->_currentEntry = (JSONCompoundEntry){ .Key = NULL, .Value = { .Type = JSONValueType_None } };
    return &Enumerator->Base;
}

static Error JSONArrayIndexedEnumerator_HasNext(void* self, bool* outHasNext)
{
    JSONArrayIndexedEnumerator* EnumeratorSelf = self;

    if (EnumeratorSelf == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outHasNext == NULL)
    {
        return CreateNullArgumentError(u8"outHasNext");
    }

    *outHasNext = (EnumeratorSelf->_currentIndex < IList_GetElementCount(&EnumeratorSelf->_array->_elements._list));
    return Error_CreateSuccess();
}

static Error JSONArrayIndexedEnumerator_NextByValue(void* self, void* outEntryValue)
{
    JSONArrayIndexedEnumerator* EnumeratorSelf = self;
    JSONArrayIndexedValue* Value = outEntryValue;
    Error Result = Error_CreateSuccess();

    if (EnumeratorSelf == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (Value == NULL)
    {
        return CreateNullArgumentError(u8"outEntryValue");
    }

    Result = IList_GetElement(&EnumeratorSelf->_array->_elements._list, EnumeratorSelf->_currentIndex, &Value->Value);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Value->Index = EnumeratorSelf->_currentIndex;
    EnumeratorSelf->_currentValue = *Value;
    EnumeratorSelf->_currentIndex++;
    return Error_CreateSuccess();
}

static Error JSONArrayIndexedEnumerator_NextByReference(void* self, void** outPointer)
{
    JSONArrayIndexedEnumerator* EnumeratorSelf = self;
    JSONObjectValue* ValuePointer = NULL;
    Error Result = Error_CreateSuccess();

    if (EnumeratorSelf == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outPointer == NULL)
    {
        return CreateNullArgumentError(u8"outPointer");
    }

    Result = IList_GetPointerToElement(&EnumeratorSelf->_array->_elements._list,
        EnumeratorSelf->_currentIndex,
        (void**)&ValuePointer);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    EnumeratorSelf->_currentValue.Index = EnumeratorSelf->_currentIndex;
    EnumeratorSelf->_currentValue.Value = *ValuePointer;
    EnumeratorSelf->_currentIndex++;
    *outPointer = &EnumeratorSelf->_currentValue;
    return Error_CreateSuccess();
}

static void JSONArrayIndexedEnumerator_Deconstruct(void* self)
{
    JSONArrayIndexedEnumerator* EnumeratorSelf = self;

    if (EnumeratorSelf == NULL)
    {
        return;
    }

    Memory_Free(EnumeratorSelf);
}

static CollectionEnumerator* JSONArray_GetIndexedEnumerator(void* self)
{
    static const CollectionEnumeratorVTable EnumeratorTemplate =
    {
        .Self = NULL,
        ._hasNext = JSONArrayIndexedEnumerator_HasNext,
        ._nextByValue = JSONArrayIndexedEnumerator_NextByValue,
        ._nextByReference = JSONArrayIndexedEnumerator_NextByReference,
        ._deconstruct = JSONArrayIndexedEnumerator_Deconstruct,
    };
    JSONArray* ArraySelf = self;
    JSONArrayIndexedEnumerator* Enumerator = NULL;

    if (ArraySelf == NULL)
    {
        return NULL;
    }

    Enumerator = Memory_Allocate(sizeof(*Enumerator));
    if (Enumerator == NULL)
    {
        return NULL;
    }

    Enumerator->Base._singleElementSize = sizeof(JSONArrayIndexedValue);
    Enumerator->Base._flags = EnumeratorFlags_CanReturnByReference;
    Enumerator->Base._vtable = EnumeratorTemplate;
    Enumerator->Base._vtable.Self = Enumerator;
    Enumerator->_array = ArraySelf;
    Enumerator->_currentIndex = 0;
    Enumerator->_currentValue = (JSONArrayIndexedValue){ .Index = 0, .Value = { .Type = JSONValueType_None } };
    return &Enumerator->Base;
}

static void InitializeCompoundCollection(JSONCompound* self)
{
    static const ICollectionVtable EntryCollectionTemplate =
    {
        .Self = NULL,
        ._getEnumerator = JSONCompound_GetEntryEnumerator,
    };

    self->_entryCollection._vtable = EntryCollectionTemplate;
    self->_entryCollection._vtable.Self = self;
}

static void InitializeArrayCollection(JSONArray* self)
{
    static const ICollectionVtable IndexedCollectionTemplate =
    {
        .Self = NULL,
        ._getEnumerator = JSONArray_GetIndexedEnumerator,
    };

    self->_indexedCollection._vtable = IndexedCollectionTemplate;
    self->_indexedCollection._vtable.Self = self;
}

static void JSONParser_SkipWhitespace(JSONParser* self)
{
    while ((self->Index < self->Length) && IsWhitespaceByte(self->Data[self->Index]))
    {
        self->Index++;
    }
}

static bool JSONParser_IsAtEnd(JSONParser* self)
{
    return self->Index >= self->Length;
}

static Error JSONParser_Peek(JSONParser* self, unsigned char* outByte)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outByte == NULL)
    {
        return CreateNullArgumentError(u8"outByte");
    }
    if (JSONParser_IsAtEnd(self))
    {
        return CreateInvalidJSONError(self->Index, u8"unexpected end of input");
    }

    *outByte = self->Data[self->Index];
    return Error_CreateSuccess();
}

static Error JSONParser_Consume(JSONParser* self, unsigned char expectedByte)
{
    unsigned char CurrentByte = 0;
    Error Result = JSONParser_Peek(self, &CurrentByte);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (CurrentByte != expectedByte)
    {
        return CreateInvalidJSONError(self->Index, u8"unexpected token");
    }

    self->Index++;
    return Error_CreateSuccess();
}

static Error JSONParser_ConsumeLiteral(JSONParser* self, const unsigned char* literal)
{
    size_t LiteralLength = GetCStringLength(literal);

    if ((self->Length - self->Index) < LiteralLength)
    {
        return CreateInvalidJSONError(self->Index, u8"unexpected end while reading literal");
    }
    if (!BytesEqual(&self->Data[self->Index], LiteralLength, literal, LiteralLength))
    {
        return CreateInvalidJSONError(self->Index, u8"unexpected literal");
    }

    self->Index += LiteralLength;
    return Error_CreateSuccess();
}

static Error JSONParser_ParseHexWord(JSONParser* self, uint16_t* outValue)
{
    uint16_t Value = 0;

    if (outValue == NULL)
    {
        return CreateNullArgumentError(u8"outValue");
    }
    if ((self->Length - self->Index) < 4)
    {
        return CreateInvalidJSONError(self->Index, u8"truncated unicode escape");
    }

    for (size_t Index = 0; Index < 4; Index++)
    {
        unsigned char Byte = self->Data[self->Index + Index];

        if (!IsHexDigitByte(Byte))
        {
            return CreateInvalidJSONError(self->Index + Index, u8"invalid unicode escape digit");
        }

        Value = (uint16_t)((Value << 4) | GetHexDigitValue(Byte));
    }

    self->Index += 4;
    *outValue = Value;
    return Error_CreateSuccess();
}

static Error JSONParser_AppendCodePoint(GenericBuffer* destination, CodePoint codePoint)
{
    unsigned char Buffer[CODEPOINT_BYTE_COUNT_MAX];
    size_t ByteCount = 0;

    if (!CharUTF8_IsCodePointValid(codePoint))
    {
        return CreateInvalidJSONError(0, u8"invalid unicode codepoint");
    }

    ByteCount = CharUTF8_WriteCodePoint(Buffer, codePoint);
    return AppendBytes(destination, Buffer, ByteCount, u8"append parsed string codepoint");
}

static Error JSONParser_ParseString(JSONParser* self, GenericBuffer** outStringBuffer)
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

    *outStringBuffer = NULL;
    Result = JSONObjectPool_BorrowString(self->Pool, &StringBuffer);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = JSONParser_Consume(self, JSON_BYTE_QUOTE);
    if (Result.Code != ErrorCode_Success)
    {
        JSONObjectPool_ReturnString(self->Pool, StringBuffer);
        return Result;
    }

    while (!JSONParser_IsAtEnd(self))
    {
        unsigned char Byte = self->Data[self->Index];

        if (Byte == JSON_BYTE_QUOTE)
        {
            self->Index++;
            *outStringBuffer = StringBuffer;
            return Error_CreateSuccess();
        }
        if (Byte == JSON_BYTE_BACKSLASH)
        {
            self->Index++;
            if (JSONParser_IsAtEnd(self))
            {
                JSONObjectPool_ReturnString(self->Pool, StringBuffer);
                return CreateInvalidJSONError(self->Index, u8"truncated string escape");
            }

            Byte = self->Data[self->Index];
            self->Index++;
            switch (Byte)
            {
                case u8'"':
                    Result = AppendByte(StringBuffer, JSON_BYTE_QUOTE, u8"append escaped quote");
                    break;
                case u8'\\':
                    Result = AppendByte(StringBuffer, JSON_BYTE_BACKSLASH, u8"append escaped backslash");
                    break;
                case u8'/':
                    Result = AppendByte(StringBuffer, JSON_BYTE_SLASH, u8"append escaped slash");
                    break;
                case u8'b':
                    Result = AppendByte(StringBuffer, JSON_BYTE_BACKSPACE, u8"append escaped backspace");
                    break;
                case u8'f':
                    Result = AppendByte(StringBuffer, JSON_BYTE_FORM_FEED, u8"append escaped form feed");
                    break;
                case u8'n':
                    Result = AppendByte(StringBuffer, JSON_BYTE_NEWLINE, u8"append escaped newline");
                    break;
                case u8'r':
                    Result = AppendByte(StringBuffer, JSON_BYTE_CARRIAGE_RETURN, u8"append escaped carriage return");
                    break;
                case u8't':
                    Result = AppendByte(StringBuffer, JSON_BYTE_TAB, u8"append escaped tab");
                    break;
                case u8'u':
                {
                    uint16_t Word1 = 0;
                    uint16_t Word2 = 0;
                    CodePoint ParsedCodePoint = 0;

                    Result = JSONParser_ParseHexWord(self, &Word1);
                    if (Result.Code != ErrorCode_Success)
                    {
                        break;
                    }
                    if (IsHighSurrogate(Word1))
                    {
                        if ((self->Length - self->Index) < 6)
                        {
                            Result = CreateInvalidJSONError(self->Index, u8"truncated surrogate pair");
                            break;
                        }
                        if ((self->Data[self->Index] != JSON_BYTE_BACKSLASH) || (self->Data[self->Index + 1] != u8'u'))
                        {
                            Result = CreateInvalidJSONError(self->Index, u8"missing low surrogate");
                            break;
                        }

                        self->Index += 2;
                        Result = JSONParser_ParseHexWord(self, &Word2);
                        if (Result.Code != ErrorCode_Success)
                        {
                            break;
                        }
                        if (!IsLowSurrogate(Word2))
                        {
                            Result = CreateInvalidJSONError(self->Index - 4, u8"invalid low surrogate");
                            break;
                        }

                        ParsedCodePoint = DecodeSurrogatePair(Word1, Word2);
                    }
                    else if (IsLowSurrogate(Word1))
                    {
                        Result = CreateInvalidJSONError(self->Index - 4, u8"unexpected low surrogate");
                        break;
                    }
                    else
                    {
                        ParsedCodePoint = (CodePoint)Word1;
                    }

                    Result = JSONParser_AppendCodePoint(StringBuffer, ParsedCodePoint);
                    break;
                }
                default:
                    Result = CreateInvalidJSONError(self->Index - 1, u8"invalid string escape");
                    break;
            }

            if (Result.Code != ErrorCode_Success)
            {
                JSONObjectPool_ReturnString(self->Pool, StringBuffer);
                return Result;
            }

            continue;
        }
        if (Byte < JSON_BYTE_SPACE)
        {
            JSONObjectPool_ReturnString(self->Pool, StringBuffer);
            return CreateInvalidJSONError(self->Index, u8"control characters must be escaped in strings");
        }
        if (!CharUTF8_IsCharBufferValid(&self->Data[self->Index], self->Length - self->Index))
        {
            JSONObjectPool_ReturnString(self->Pool, StringBuffer);
            return CreateInvalidJSONError(self->Index, u8"string contains invalid UTF-8");
        }

        {
            size_t ByteCount = CharUTF8_GetByteCountChar(&self->Data[self->Index]);

            Result = AppendBytes(StringBuffer, &self->Data[self->Index], ByteCount, u8"append parsed string bytes");
            if (Result.Code != ErrorCode_Success)
            {
                JSONObjectPool_ReturnString(self->Pool, StringBuffer);
                return Result;
            }

            self->Index += ByteCount;
        }
    }

    JSONObjectPool_ReturnString(self->Pool, StringBuffer);
    return CreateInvalidJSONError(self->Index, u8"unterminated string");
}

static Error JSONParser_ParseNumber(JSONParser* self, JSONObjectValue* outValue)
{
    size_t StartIndex = 0;
    bool IsRealNumber = false;
    GenericBuffer TokenBuffer;
    int64_t IntegerValue = 0;
    double DoubleValue = 0.0;
    Error Result = Error_CreateSuccess();

    if (outValue == NULL)
    {
        return CreateNullArgumentError(u8"outValue");
    }

    GenericBuffer_AllocateVariable(&TokenBuffer, 0, sizeof(unsigned char));
    StartIndex = self->Index;
    if (self->Data[self->Index] == JSON_BYTE_MINUS)
    {
        self->Index++;
        if (JSONParser_IsAtEnd(self))
        {
            Result = CreateInvalidJSONError(self->Index, u8"truncated number");
            goto cleanup;
        }
    }

    if (self->Data[self->Index] == JSON_BYTE_ZERO)
    {
        self->Index++;
        if ((!JSONParser_IsAtEnd(self)) && IsDigitByte(self->Data[self->Index]))
        {
            Result = CreateInvalidJSONError(self->Index, u8"leading zeros are not allowed");
            goto cleanup;
        }
    }
    else
    {
        if (!IsDigitByte(self->Data[self->Index]))
        {
            Result = CreateInvalidJSONError(self->Index, u8"expected a digit");
            goto cleanup;
        }

        while ((!JSONParser_IsAtEnd(self)) && IsDigitByte(self->Data[self->Index]))
        {
            self->Index++;
        }
    }

    if ((!JSONParser_IsAtEnd(self)) && (self->Data[self->Index] == JSON_BYTE_PERIOD))
    {
        IsRealNumber = true;
        self->Index++;
        if (JSONParser_IsAtEnd(self) || !IsDigitByte(self->Data[self->Index]))
        {
            Result = CreateInvalidJSONError(self->Index, u8"fractional part requires at least one digit");
            goto cleanup;
        }

        while ((!JSONParser_IsAtEnd(self)) && IsDigitByte(self->Data[self->Index]))
        {
            self->Index++;
        }
    }

    if ((!JSONParser_IsAtEnd(self))
        && ((self->Data[self->Index] == JSON_BYTE_LOWER_E) || (self->Data[self->Index] == JSON_BYTE_UPPER_E)))
    {
        IsRealNumber = true;
        self->Index++;
        if ((!JSONParser_IsAtEnd(self))
            && ((self->Data[self->Index] == JSON_BYTE_PLUS) || (self->Data[self->Index] == JSON_BYTE_MINUS)))
        {
            self->Index++;
        }
        if (JSONParser_IsAtEnd(self) || !IsDigitByte(self->Data[self->Index]))
        {
            Result = CreateInvalidJSONError(self->Index, u8"exponent requires at least one digit");
            goto cleanup;
        }

        while ((!JSONParser_IsAtEnd(self)) && IsDigitByte(self->Data[self->Index]))
        {
            self->Index++;
        }
    }

    Result = AppendBytes(&TokenBuffer,
        &self->Data[StartIndex],
        self->Index - StartIndex,
        u8"copy numeric token");
    if (Result.Code != ErrorCode_Success)
    {
        goto cleanup;
    }
    Result = AppendByte(&TokenBuffer, 0, u8"terminate numeric token");
    if (Result.Code != ErrorCode_Success)
    {
        goto cleanup;
    }

    if (IsRealNumber)
    {
        Result = Number_DoubleFromString(TokenBuffer._data, &DoubleValue, DecimalSeparator_Period);
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            Result = CreateInvalidJSONError(StartIndex, u8"invalid real number");
            goto cleanup;
        }

        *outValue = JSONObjectValue_CreateRealNumber(DoubleValue);
    }
    else
    {
        Result = Number_Int64FromString(TokenBuffer._data, NUMBER_BASE_10, &IntegerValue);
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            Result = CreateInvalidJSONError(StartIndex, u8"integer value is out of range for int64");
            goto cleanup;
        }

        *outValue = JSONObjectValue_CreateInteger(IntegerValue);
    }

    Result = Error_CreateSuccess();

cleanup:
    Memory_Free(TokenBuffer._data);
    return Result;
}

static Error JSONParser_ParseValue(JSONParser* self, JSONObjectValue* outValue);

static Error JSONParser_ParseObject(JSONParser* self, JSONObjectValue* outValue)
{
    JSONCompound* Compound = NULL;
    Error Result = JSONObjectPool_BorrowCompound(self->Pool, &Compound);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = JSONParser_Consume(self, JSON_BYTE_OPEN_BRACE);
    if (Result.Code != ErrorCode_Success)
    {
        JSONObjectPool_ReturnCompound(self->Pool, Compound, true);
        return Result;
    }

    JSONParser_SkipWhitespace(self);
    if (!JSONParser_IsAtEnd(self) && (self->Data[self->Index] == JSON_BYTE_CLOSE_BRACE))
    {
        self->Index++;
        *outValue = JSONObjectValue_CreateCompound(Compound);
        return Error_CreateSuccess();
    }

    while (true)
    {
        GenericBuffer* KeyBuffer = NULL;
        JSONObjectValue ElementValue;

        JSONParser_SkipWhitespace(self);
        Result = JSONParser_ParseString(self, &KeyBuffer);
        if (Result.Code != ErrorCode_Success)
        {
            JSONObjectPool_ReturnCompound(self->Pool, Compound, true);
            return Result;
        }

        JSONParser_SkipWhitespace(self);
        Result = JSONParser_Consume(self, JSON_BYTE_COLON);
        if (Result.Code != ErrorCode_Success)
        {
            JSONObjectPool_ReturnString(self->Pool, KeyBuffer);
            JSONObjectPool_ReturnCompound(self->Pool, Compound, true);
            return Result;
        }

        JSONParser_SkipWhitespace(self);
        Result = JSONParser_ParseValue(self, &ElementValue);
        if (Result.Code != ErrorCode_Success)
        {
            JSONObjectPool_ReturnString(self->Pool, KeyBuffer);
            JSONObjectPool_ReturnCompound(self->Pool, Compound, true);
            return Result;
        }

        Result = JSONCompound_SetOwnedKey(Compound, KeyBuffer->_data, KeyBuffer->_count, &ElementValue);
        {
            Error ReturnStringResult = JSONObjectPool_ReturnString(self->Pool, KeyBuffer);

            if (Result.Code == ErrorCode_Success)
            {
                Result = ReturnStringResult;
            }
        }
        if (Result.Code != ErrorCode_Success)
        {
            JSONObjectPool_ReturnValue(self->Pool, &ElementValue);
            JSONObjectPool_ReturnCompound(self->Pool, Compound, true);
            return Result;
        }

        JSONParser_SkipWhitespace(self);
        if (JSONParser_IsAtEnd(self))
        {
            JSONObjectPool_ReturnCompound(self->Pool, Compound, true);
            return CreateInvalidJSONError(self->Index, u8"unterminated object");
        }
        if (self->Data[self->Index] == JSON_BYTE_CLOSE_BRACE)
        {
            self->Index++;
            *outValue = JSONObjectValue_CreateCompound(Compound);
            return Error_CreateSuccess();
        }

        Result = JSONParser_Consume(self, JSON_BYTE_COMMA);
        if (Result.Code != ErrorCode_Success)
        {
            JSONObjectPool_ReturnCompound(self->Pool, Compound, true);
            return Result;
        }
    }
}

static Error JSONParser_ParseArray(JSONParser* self, JSONObjectValue* outValue)
{
    JSONArray* Array = NULL;
    Error Result = JSONObjectPool_BorrowArray(self->Pool, &Array);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = JSONParser_Consume(self, JSON_BYTE_OPEN_BRACKET);
    if (Result.Code != ErrorCode_Success)
    {
        JSONObjectPool_ReturnArray(self->Pool, Array, true);
        return Result;
    }

    JSONParser_SkipWhitespace(self);
    if (!JSONParser_IsAtEnd(self) && (self->Data[self->Index] == JSON_BYTE_CLOSE_BRACKET))
    {
        self->Index++;
        *outValue = JSONObjectValue_CreateArray(Array);
        return Error_CreateSuccess();
    }

    while (true)
    {
        JSONObjectValue ElementValue;

        JSONParser_SkipWhitespace(self);
        Result = JSONParser_ParseValue(self, &ElementValue);
        if (Result.Code != ErrorCode_Success)
        {
            JSONObjectPool_ReturnArray(self->Pool, Array, true);
            return Result;
        }

        Result = JSONArray_Add(Array, &ElementValue);
        if (Result.Code != ErrorCode_Success)
        {
            JSONObjectPool_ReturnValue(self->Pool, &ElementValue);
            JSONObjectPool_ReturnArray(self->Pool, Array, true);
            return Result;
        }

        JSONParser_SkipWhitespace(self);
        if (JSONParser_IsAtEnd(self))
        {
            JSONObjectPool_ReturnArray(self->Pool, Array, true);
            return CreateInvalidJSONError(self->Index, u8"unterminated array");
        }
        if (self->Data[self->Index] == JSON_BYTE_CLOSE_BRACKET)
        {
            self->Index++;
            *outValue = JSONObjectValue_CreateArray(Array);
            return Error_CreateSuccess();
        }

        Result = JSONParser_Consume(self, JSON_BYTE_COMMA);
        if (Result.Code != ErrorCode_Success)
        {
            JSONObjectPool_ReturnArray(self->Pool, Array, true);
            return Result;
        }
    }
}

static Error JSONParser_ParseValue(JSONParser* self, JSONObjectValue* outValue)
{
    unsigned char Byte = 0;
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outValue == NULL)
    {
        return CreateNullArgumentError(u8"outValue");
    }

    JSONParser_SkipWhitespace(self);
    Result = JSONParser_Peek(self, &Byte);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    switch (Byte)
    {
        case u8'{':
            return JSONParser_ParseObject(self, outValue);
        case u8'[':
            return JSONParser_ParseArray(self, outValue);
        case u8'"':
        {
            GenericBuffer* StringBuffer = NULL;

            Result = JSONParser_ParseString(self, &StringBuffer);
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }

            *outValue = JSONObjectValue_CreateString(StringBuffer);
            return Error_CreateSuccess();
        }
        case u8't':
            Result = JSONParser_ConsumeLiteral(self, JSON_LITERAL_TRUE);
            if (Result.Code == ErrorCode_Success)
            {
                *outValue = JSONObjectValue_CreateBoolean(true);
            }
            return Result;
        case u8'f':
            Result = JSONParser_ConsumeLiteral(self, JSON_LITERAL_FALSE);
            if (Result.Code == ErrorCode_Success)
            {
                *outValue = JSONObjectValue_CreateBoolean(false);
            }
            return Result;
        case u8'n':
            Result = JSONParser_ConsumeLiteral(self, JSON_LITERAL_NULL);
            if (Result.Code == ErrorCode_Success)
            {
                *outValue = JSONObjectValue_CreateNull();
            }
            return Result;
        default:
            if ((Byte == JSON_BYTE_MINUS) || IsDigitByte(Byte))
            {
                return JSONParser_ParseNumber(self, outValue);
            }

            return CreateInvalidJSONError(self->Index, u8"unexpected token");
    }
}

static Error SerializeString(GenericBuffer* source, GenericBuffer* destination)
{
    Error Result = Error_CreateSuccess();
    size_t Index = 0;

    Result = AppendByte(destination, JSON_BYTE_QUOTE, u8"write JSON string opening quote");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    while (Index < source->_count)
    {
        CodePoint CurrentCodePoint = 0;
        size_t ByteCount = 0;

        if (!CharUTF8_IsCharBufferValid(&source->_data[Index], source->_count - Index))
        {
            return CreateSerializeError(u8"string contains invalid UTF-8");
        }

        ByteCount = CharUTF8_GetByteCountChar(&source->_data[Index]);
        CurrentCodePoint = CharUTF8_GetCodePoint(&source->_data[Index]);
        switch (CurrentCodePoint)
        {
            case u8'"':
                Result = AppendCString(destination, JSON_LITERAL_ESCAPE_QUOTE, u8"write escaped quote");
                break;
            case u8'\\':
                Result = AppendCString(destination, JSON_LITERAL_ESCAPE_BACKSLASH, u8"write escaped backslash");
                break;
            case u8'\b':
                Result = AppendCString(destination, JSON_LITERAL_ESCAPE_BACKSPACE, u8"write escaped backspace");
                break;
            case u8'\f':
                Result = AppendCString(destination, JSON_LITERAL_ESCAPE_FORM_FEED, u8"write escaped form feed");
                break;
            case u8'\n':
                Result = AppendCString(destination, JSON_LITERAL_ESCAPE_NEWLINE, u8"write escaped newline");
                break;
            case u8'\r':
                Result = AppendCString(destination, JSON_LITERAL_ESCAPE_CARRIAGE_RETURN, u8"write escaped carriage return");
                break;
            case u8'\t':
                Result = AppendCString(destination, JSON_LITERAL_ESCAPE_TAB, u8"write escaped tab");
                break;
            default:
                if ((CurrentCodePoint >= 0) && ((uint32_t)CurrentCodePoint < UINT32_C(0x20)))
                {
                    uint16_t Word = (uint16_t)CurrentCodePoint;
                    unsigned char Digits[4];

                    Result = AppendCString(destination, JSON_LITERAL_ESCAPE_UNICODE_PREFIX, u8"write unicode escape prefix");
                    if (Result.Code != ErrorCode_Success)
                    {
                        return Result;
                    }

                    for (size_t DigitIndex = 0; DigitIndex < 4; DigitIndex++)
                    {
                        uint16_t Shift = (uint16_t)((3U - DigitIndex) * 4U);
                        uint16_t Nibble = (uint16_t)((Word >> Shift) & UINT16_C(0x000F));

                        Digits[DigitIndex] = (Nibble < 10U)
                            ? (unsigned char)(JSON_BYTE_ZERO + Nibble)
                            : (unsigned char)(u8'A' + (Nibble - 10U));
                    }

                    Result = AppendBytes(destination, Digits, 4, u8"write unicode escape");
                }
                else
                {
                    Result = AppendBytes(destination,
                        &source->_data[Index],
                        ByteCount,
                        u8"write UTF-8 string bytes");
                }
                break;
        }

        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        Index += ByteCount;
    }

    return AppendByte(destination, JSON_BYTE_QUOTE, u8"write JSON string closing quote");
}

static Error SerializeIndentation(GenericBuffer* destination, size_t depth)
{
    size_t TotalSpaces = 0;

    if (!TryAddSize(0, depth * JSON_INDENT_SIZE, &TotalSpaces))
    {
        return CreateSerializeError(u8"indentation depth exceeds supported range");
    }

    for (size_t Index = 0; Index < TotalSpaces; Index++)
    {
        Error Result = AppendByte(destination, JSON_BYTE_SPACE, u8"write indentation");

        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    return Error_CreateSuccess();
}

static Error SerializeValue(JSONObjectValue* value,
    GenericBuffer* destination,
    JSONSerializeFlags flags,
    size_t depth);

static Error SerializeCompound(JSONCompound* value,
    GenericBuffer* destination,
    JSONSerializeFlags flags,
    size_t depth)
{
    bool IsPretty = (flags & JSONSerializeFlags_PrettyPrinted) != 0;
    CollectionEnumerator* Enumerator = NULL;
    bool HasNext = false;
    Error Result = Error_CreateSuccess();

    if (value == NULL)
    {
        return CreateSerializeError(u8"compound value is null");
    }

    Result = AppendByte(destination, JSON_BYTE_OPEN_BRACE, u8"write object opening brace");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (JSONCompound_GetEntryCount(value) == 0)
    {
        return AppendByte(destination, JSON_BYTE_CLOSE_BRACE, u8"write object closing brace");
    }
    if (IsPretty)
    {
        Result = AppendByte(destination, JSON_BYTE_NEWLINE, u8"write object newline");
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    Enumerator = ICollection_GetEnumerator(JSONCompound_GetEntryCollection(value));
    if (Enumerator == NULL)
    {
        return CreateSerializeError(u8"could not enumerate object entries");
    }

    Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
    while ((Result.Code == ErrorCode_Success) && HasNext)
    {
        JSONCompoundEntry Entry;

        Result = CollectionEnumerator_NextByValue(Enumerator, &Entry);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }
        if (IsPretty)
        {
            Result = SerializeIndentation(destination, depth + 1);
            if (Result.Code != ErrorCode_Success)
            {
                break;
            }
        }

        {
            GenericBuffer KeyView;

            GenericBuffer_CreateConstant(&KeyView,
                (void*)Entry.Key,
                GetCStringLength(Entry.Key),
                sizeof(unsigned char),
                GetCStringLength(Entry.Key));
            Result = SerializeString(&KeyView, destination);
        }
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }

        Result = AppendByte(destination, JSON_BYTE_COLON, u8"write object colon");
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }
        if (IsPretty)
        {
            Result = AppendByte(destination, JSON_BYTE_SPACE, u8"write object space");
            if (Result.Code != ErrorCode_Success)
            {
                break;
            }
        }

        Result = SerializeValue(&Entry.Value, destination, flags, depth + 1);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }

        Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }
        if (HasNext)
        {
            Result = AppendByte(destination, JSON_BYTE_COMMA, u8"write object comma");
            if (Result.Code != ErrorCode_Success)
            {
                break;
            }
        }
        if (IsPretty)
        {
            Result = AppendByte(destination, JSON_BYTE_NEWLINE, u8"write object newline");
            if (Result.Code != ErrorCode_Success)
            {
                break;
            }
        }
    }

    CollectionEnumerator_Deconstruct(Enumerator);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (IsPretty)
    {
        Result = SerializeIndentation(destination, depth);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    return AppendByte(destination, JSON_BYTE_CLOSE_BRACE, u8"write object closing brace");
}

static Error SerializeArray(JSONArray* value,
    GenericBuffer* destination,
    JSONSerializeFlags flags,
    size_t depth)
{
    bool IsPretty = (flags & JSONSerializeFlags_PrettyPrinted) != 0;
    size_t ElementCount = 0;
    Error Result = Error_CreateSuccess();

    if (value == NULL)
    {
        return CreateSerializeError(u8"array value is null");
    }

    Result = AppendByte(destination, JSON_BYTE_OPEN_BRACKET, u8"write array opening bracket");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    ElementCount = JSONArray_GetElementCount(value);
    if (ElementCount == 0)
    {
        return AppendByte(destination, JSON_BYTE_CLOSE_BRACKET, u8"write array closing bracket");
    }
    if (IsPretty)
    {
        Result = AppendByte(destination, JSON_BYTE_NEWLINE, u8"write array newline");
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    for (size_t Index = 0; Index < ElementCount; Index++)
    {
        JSONObjectValue ElementValue;

        Result = JSONArray_Get(value, Index, &ElementValue);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        if (IsPretty)
        {
            Result = SerializeIndentation(destination, depth + 1);
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }
        }

        Result = SerializeValue(&ElementValue, destination, flags, depth + 1);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        if ((Index + 1) < ElementCount)
        {
            Result = AppendByte(destination, JSON_BYTE_COMMA, u8"write array comma");
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }
        }
        if (IsPretty)
        {
            Result = AppendByte(destination, JSON_BYTE_NEWLINE, u8"write array newline");
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }
        }
    }

    if (IsPretty)
    {
        Result = SerializeIndentation(destination, depth);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    return AppendByte(destination, JSON_BYTE_CLOSE_BRACKET, u8"write array closing bracket");
}

static Error SerializeValue(JSONObjectValue* value,
    GenericBuffer* destination,
    JSONSerializeFlags flags,
    size_t depth)
{
    GenericBuffer NumberBuffer;
    Error Result = Error_CreateSuccess();

    UNUSED(depth);
    if (value == NULL)
    {
        return CreateNullArgumentError(u8"value");
    }

    switch (value->Type)
    {
        case JSONValueType_String:
            if (value->Value.String == NULL)
            {
                return CreateSerializeError(u8"string value is null");
            }
            return SerializeString(value->Value.String, destination);
        case JSONValueType_Boolean:
            return AppendCString(destination, value->Value.Boolean ? JSON_LITERAL_TRUE : JSON_LITERAL_FALSE, u8"write boolean literal");
        case JSONValueType_Integer:
            GenericBuffer_AllocateVariable(&NumberBuffer, 0, sizeof(unsigned char));
            Result = Number_Int64ToString(value->Value.Integer, NUMBER_BASE_10, false, &NumberBuffer);
            if (Result.Code == ErrorCode_Success)
            {
                while ((NumberBuffer._count > 0) && (NumberBuffer._data[NumberBuffer._count - 1] == 0))
                {
                    NumberBuffer._count--;
                }
                Result = AppendBytes(destination, NumberBuffer._data, NumberBuffer._count, u8"write integer");
            }
            Memory_Free(NumberBuffer._data);
            return Result;
        case JSONValueType_RealNumber:
            if (!isfinite(value->Value.RealNumber))
            {
                return CreateSerializeError(u8"non-finite real numbers are not valid JSON");
            }
            GenericBuffer_AllocateVariable(&NumberBuffer, 0, sizeof(unsigned char));
            Result = Number_DoubleToString(value->Value.RealNumber,
                &NumberBuffer,
                DecimalFormatOptions_CreateShortest(DecimalSeparator_Period));
            if (Result.Code == ErrorCode_Success)
            {
                while ((NumberBuffer._count > 0) && (NumberBuffer._data[NumberBuffer._count - 1] == 0))
                {
                    NumberBuffer._count--;
                }
                Result = AppendBytes(destination, NumberBuffer._data, NumberBuffer._count, u8"write real number");
            }
            Memory_Free(NumberBuffer._data);
            return Result;
        case JSONValueType_Null:
            return AppendCString(destination, JSON_LITERAL_NULL, u8"write null literal");
        case JSONValueType_Compound:
            return SerializeCompound(value->Value.Compound, destination, flags, depth);
        case JSONValueType_Array:
            return SerializeArray(value->Value.Array, destination, flags, depth);
        default:
            return CreateInvalidTypeError(value->Type);
    }
}


// Public functions.
Error JSON_Serialize(JSONObjectValue* value, GenericBuffer* destination, JSONSerializeFlags flags)
{
    Error Result = Error_CreateSuccess();

    Result = ValidateByteBuffer(destination, u8"destination");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (value == NULL)
    {
        return CreateNullArgumentError(u8"value");
    }

    Result = SerializeValue(value, destination, flags, 0);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((flags & JSONSerializeFlags_NullTerminated) != 0)
    {
        Result = AppendByte(destination, 0, u8"append terminating null byte");
    }

    return Result;
}

Error JSON_Deserialize(JSONObjectPool* pool, GenericBuffer* source, JSONObjectValue* outValue)
{
    JSONParser Parser;
    Error Result = Error_CreateSuccess();

    if (pool == NULL)
    {
        return CreateNullArgumentError(u8"pool");
    }
    Result = ValidateByteBuffer(source, u8"source");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (outValue == NULL)
    {
        return CreateNullArgumentError(u8"outValue");
    }

    Parser.Pool = pool;
    Parser.Data = source->_data;
    Parser.Length = source->_count;
    Parser.Index = 0;
    *outValue = (JSONObjectValue){ .Type = JSONValueType_None };

    Result = JSONParser_ParseValue(&Parser, outValue);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    JSONParser_SkipWhitespace(&Parser);
    if (Parser.Index != Parser.Length)
    {
        JSONObjectPool_ReturnValue(pool, outValue);
        *outValue = (JSONObjectValue){ .Type = JSONValueType_None };
        return CreateInvalidJSONError(Parser.Index, u8"unexpected trailing content");
    }

    return Error_CreateSuccess();
}

Error JSONCompound_Set(JSONCompound* self, const unsigned char* key, JSONObjectValue* value)
{
    if (key == NULL)
    {
        return CreateNullArgumentError(u8"key");
    }

    return JSONCompound_SetOwnedKey(self, key, GetCStringLength(key), value);
}

Error JSONCompound_Get(JSONCompound* self, const unsigned char* key, JSONObjectValue* outValue)
{
    bool WasFound = false;
    Error Result = JSONCompound_GetOptionalInternal(self, key, outValue, &WasFound);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!WasFound)
    {
        return CreateNotFoundError(u8"compound entry");
    }

    return Error_CreateSuccess();
}

Error JSONCompound_GetOptional(JSONCompound* self,
    const unsigned char* key,
    JSONObjectValue* outValue,
    bool* outWasFound)
{
    return JSONCompound_GetOptionalInternal(self, key, outValue, outWasFound);
}

Error JSONCompound_GetVerified(JSONCompound* self,
    const unsigned char* key,
    JSONValueType expectedType,
    JSONObjectValue* outValue)
{
    Error Result = JSONCompound_Get(self, key, outValue);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (outValue->Type != expectedType)
    {
        return CreateTypeMismatchError(expectedType, outValue->Type);
    }

    return Error_CreateSuccess();
}

Error JSONCompound_GetOptionalVerified(JSONCompound* self,
    const unsigned char* key,
    JSONValueType expectedType,
    JSONObjectValue* outValue,
    bool* outWasFound)
{
    Error Result = JSONCompound_GetOptionalInternal(self, key, outValue, outWasFound);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (*outWasFound && (outValue->Type != expectedType))
    {
        return CreateTypeMismatchError(expectedType, outValue->Type);
    }

    return Error_CreateSuccess();
}

Error JSONCompound_Clear(JSONCompound* self)
{
    CollectionEnumerator* Enumerator = NULL;
    bool HasNext = false;
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    Enumerator = ICollection_GetEnumerator(IMap_AsValueCollection(HashMap_AsMap(&self->_elements)));
    if (Enumerator == NULL)
    {
        return CreateCapacityError(u8"enumerate JSON compound values");
    }

    Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
    while ((Result.Code == ErrorCode_Success) && HasNext)
    {
        JSONObjectValue Value;

        Result = CollectionEnumerator_NextByValue(Enumerator, &Value);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }

        Result = JSONObjectPool_ReturnValue(self->_pool, &Value);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }

        Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
    }

    CollectionEnumerator_Deconstruct(Enumerator);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return IMap_Clear(HashMap_AsMap(&self->_elements));
}

Error JSONCompound_Remove(JSONCompound* self, const unsigned char* key)
{
    JSONObjectValue Value;
    bool WasFound = false;
    Error Result = JSONCompound_GetOptionalInternal(self, key, &Value, &WasFound);
    bool WasRemoved = false;

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!WasFound)
    {
        return CreateNotFoundError(u8"compound entry");
    }

    Result = JSONObjectPool_ReturnValue(self->_pool, &Value);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = IMap_Remove(HashMap_AsMap(&self->_elements), &key, &WasRemoved);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!WasRemoved)
    {
        return CreateNotFoundError(u8"compound entry");
    }

    return Error_CreateSuccess();
}

size_t JSONCompound_GetEntryCount(JSONCompound* self)
{
    if (self == NULL)
    {
        return 0;
    }

    return IMap_GetEntryCount(HashMap_AsMap(&self->_elements));
}

ICollection* JSONCompound_GetEntryCollection(JSONCompound* self)
{
    if (self == NULL)
    {
        return NULL;
    }

    return &self->_entryCollection;
}

ICollection* JSONCompound_GetValueCollection(JSONCompound* self)
{
    if (self == NULL)
    {
        return NULL;
    }

    return IMap_AsValueCollection(HashMap_AsMap(&self->_elements));
}

ICollection* JSONCompound_GetKeyCollection(JSONCompound* self)
{
    if (self == NULL)
    {
        return NULL;
    }

    return IMap_AsKeyCollection(HashMap_AsMap(&self->_elements));
}

Error JSONArray_Replace(JSONArray* self, size_t index, JSONObjectValue* value)
{
    JSONObjectValue ExistingValue;
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (value == NULL)
    {
        return CreateNullArgumentError(u8"value");
    }

    Result = ValidatePoolValueOwner(self->_pool, value);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = JSONArray_Get(self, index, &ExistingValue);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = JSONObjectPool_ReturnValue(self->_pool, &ExistingValue);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return IList_Replace(&self->_elements._list, index, value);
}

Error JSONArray_Add(JSONArray* self, JSONObjectValue* value)
{
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (value == NULL)
    {
        return CreateNullArgumentError(u8"value");
    }

    Result = ValidatePoolValueOwner(self->_pool, value);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return IList_AddLast(&self->_elements._list, value);
}

Error JSONArray_Insert(JSONArray* self, size_t index, JSONObjectValue* value)
{
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (value == NULL)
    {
        return CreateNullArgumentError(u8"value");
    }

    Result = ValidatePoolValueOwner(self->_pool, value);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return IList_Insert(&self->_elements._list, index, value);
}

Error JSONArray_Get(JSONArray* self, size_t index, JSONObjectValue* outValue)
{
    bool WasFound = false;
    Error Result = JSONArray_GetOptionalInternal(self, index, outValue, &WasFound);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!WasFound)
    {
        return CreateNotFoundError(u8"array element");
    }

    return Error_CreateSuccess();
}

Error JSONArray_GetOptional(JSONArray* self, size_t index, JSONObjectValue* outValue, bool* outWasFound)
{
    return JSONArray_GetOptionalInternal(self, index, outValue, outWasFound);
}

Error JSONArray_GetVerified(JSONArray* self, size_t index, JSONValueType expectedType, JSONObjectValue* outValue)
{
    Error Result = JSONArray_Get(self, index, outValue);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (outValue->Type != expectedType)
    {
        return CreateTypeMismatchError(expectedType, outValue->Type);
    }

    return Error_CreateSuccess();
}

Error JSONArray_GetOptionalVerified(JSONArray* self,
    size_t index,
    JSONValueType expectedType,
    JSONObjectValue* outValue,
    bool* outWasFound)
{
    Error Result = JSONArray_GetOptionalInternal(self, index, outValue, outWasFound);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (*outWasFound && (outValue->Type != expectedType))
    {
        return CreateTypeMismatchError(expectedType, outValue->Type);
    }

    return Error_CreateSuccess();
}

Error JSONArray_Clear(JSONArray* self)
{
    size_t ElementCount = 0;
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    ElementCount = IList_GetElementCount(&self->_elements._list);
    for (size_t Index = 0; Index < ElementCount; Index++)
    {
        JSONObjectValue Value;

        Result = IList_GetElement(&self->_elements._list, Index, &Value);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        Result = JSONObjectPool_ReturnValue(self->_pool, &Value);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    return IList_Clear(&self->_elements._list);
}

Error JSONArray_RemoveAt(JSONArray* self, size_t index)
{
    JSONObjectValue Value;
    Error Result = JSONArray_Get(self, index, &Value);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = JSONObjectPool_ReturnValue(self->_pool, &Value);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return IList_RemoveAt(&self->_elements._list, index);
}

size_t JSONArray_GetElementCount(JSONArray* self)
{
    if (self == NULL)
    {
        return 0;
    }

    return IList_GetElementCount(&self->_elements._list);
}

ICollection* JSONArray_GetElementCollection(JSONArray* self)
{
    if (self == NULL)
    {
        return NULL;
    }

    return &self->_indexedCollection;
}

Error JSONObjectPool_Create(JSONObjectPool** outPool)
{
    JSONObjectPool* Pool = NULL;

    if (outPool == NULL)
    {
        return CreateNullArgumentError(u8"outPool");
    }

    *outPool = NULL;
    Pool = Memory_Allocate(sizeof(*Pool));
    if (Pool == NULL)
    {
        return CreateCapacityError(u8"allocate JSON object pool");
    }

    ArrayList_Construct1(&Pool->_allCompounds, sizeof(JSONCompound*));
    ArrayList_Construct1(&Pool->_availableCompounds, sizeof(JSONCompound*));
    ArrayList_Construct1(&Pool->_allArrays, sizeof(JSONArray*));
    ArrayList_Construct1(&Pool->_availableArrays, sizeof(JSONArray*));
    ArrayList_Construct1(&Pool->_allStrings, sizeof(GenericBuffer*));
    ArrayList_Construct1(&Pool->_availableStrings, sizeof(GenericBuffer*));
    ArrayList_Construct1(&Pool->_allKeys, sizeof(unsigned char*));
    *outPool = Pool;
    return Error_CreateSuccess();
}

Error JSONObjectPool_Deconstruct(JSONObjectPool* self)
{
    size_t Count = 0;
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    Count = IList_GetElementCount(&self->_allCompounds._list);
    for (size_t Index = 0; Index < Count; Index++)
    {
        JSONCompound* Compound = NULL;

        if (IList_GetElement(&self->_allCompounds._list, Index, &Compound).Code == ErrorCode_Success)
        {
            Result = HashMap_Deconstruct(&Compound->_elements);
            if (Result.Code != ErrorCode_Success)
            {
                Memory_Free(Compound);
                return Result;
            }

            Memory_Free(Compound);
        }
    }

    Count = IList_GetElementCount(&self->_allArrays._list);
    for (size_t Index = 0; Index < Count; Index++)
    {
        JSONArray* Array = NULL;

        if (IList_GetElement(&self->_allArrays._list, Index, &Array).Code == ErrorCode_Success)
        {
            ArrayList_Deconstruct(&Array->_elements);
            Memory_Free(Array);
        }
    }

    Count = IList_GetElementCount(&self->_allStrings._list);
    for (size_t Index = 0; Index < Count; Index++)
    {
        GenericBuffer* StringBuffer = NULL;

        if (IList_GetElement(&self->_allStrings._list, Index, &StringBuffer).Code == ErrorCode_Success)
        {
            Memory_Free(StringBuffer->_data);
            Memory_Free(StringBuffer);
        }
    }

    Count = IList_GetElementCount(&self->_allKeys._list);
    for (size_t Index = 0; Index < Count; Index++)
    {
        unsigned char* KeyBuffer = NULL;

        if (IList_GetElement(&self->_allKeys._list, Index, &KeyBuffer).Code == ErrorCode_Success)
        {
            Memory_Free(KeyBuffer);
        }
    }

    ArrayList_Deconstruct(&self->_allCompounds);
    ArrayList_Deconstruct(&self->_availableCompounds);
    ArrayList_Deconstruct(&self->_allArrays);
    ArrayList_Deconstruct(&self->_availableArrays);
    ArrayList_Deconstruct(&self->_allStrings);
    ArrayList_Deconstruct(&self->_availableStrings);
    ArrayList_Deconstruct(&self->_allKeys);
    Memory_Free(self);
    return Error_CreateSuccess();
}

Error JSONObjectPool_BorrowCompound(JSONObjectPool* self, JSONCompound** outCompound)
{
    JSONCompound* Compound = NULL;
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
    if (IList_GetElementCount(&self->_availableCompounds._list) > 0)
    {
        Result = IList_GetLast(&self->_availableCompounds._list, &Compound);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        Result = IList_RemoveLast(&self->_availableCompounds._list);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    else
    {
        Result = JSONObjectPool_CreateCompoundStorage(self, &Compound);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        Result = IList_AddLast(&self->_allCompounds._list, &Compound);
        if (Result.Code != ErrorCode_Success)
        {
            HashMap_Deconstruct(&Compound->_elements);
            Memory_Free(Compound);
            return Result;
        }
    }

    Result = JSONCompound_Clear(Compound);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    *outCompound = Compound;
    return Error_CreateSuccess();
}

Error JSONObjectPool_BorrowArray(JSONObjectPool* self, JSONArray** outArray)
{
    JSONArray* Array = NULL;
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
    if (IList_GetElementCount(&self->_availableArrays._list) > 0)
    {
        Result = IList_GetLast(&self->_availableArrays._list, &Array);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        Result = IList_RemoveLast(&self->_availableArrays._list);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    else
    {
        Result = JSONObjectPool_CreateArrayStorage(self, &Array);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        Result = IList_AddLast(&self->_allArrays._list, &Array);
        if (Result.Code != ErrorCode_Success)
        {
            ArrayList_Deconstruct(&Array->_elements);
            Memory_Free(Array);
            return Result;
        }
    }

    Result = JSONArray_Clear(Array);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    *outArray = Array;
    return Error_CreateSuccess();
}

Error JSONObjectPool_BorrowString(JSONObjectPool* self, GenericBuffer** outStringBuffer)
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

    *outStringBuffer = NULL;
    if (IList_GetElementCount(&self->_availableStrings._list) > 0)
    {
        Result = IList_GetLast(&self->_availableStrings._list, &StringBuffer);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        Result = IList_RemoveLast(&self->_availableStrings._list);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    else
    {
        Result = JSONObjectPool_CreateStringStorage(self, &StringBuffer);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        Result = IList_AddLast(&self->_allStrings._list, &StringBuffer);
        if (Result.Code != ErrorCode_Success)
        {
            Memory_Free(StringBuffer->_data);
            Memory_Free(StringBuffer);
            return Result;
        }
    }

    if (!GenericBuffer_Clear(StringBuffer))
    {
        return CreateCapacityError(u8"clear borrowed JSON string");
    }

    *outStringBuffer = StringBuffer;
    return Error_CreateSuccess();
}

Error JSONObjectPool_ReturnCompound(JSONObjectPool* self, JSONCompound* compound, bool includeNestedStructures)
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
    if (compound->_pool != self)
    {
        return CreatePoolMismatchError();
    }

    if (includeNestedStructures)
    {
        Result = JSONCompound_Clear(compound);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    return IList_AddLast(&self->_availableCompounds._list, &compound);
}

Error JSONObjectPool_ReturnArray(JSONObjectPool* self, JSONArray* array, bool includeNestedStructures)
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
    if (array->_pool != self)
    {
        return CreatePoolMismatchError();
    }

    if (includeNestedStructures)
    {
        Result = JSONArray_Clear(array);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    return IList_AddLast(&self->_availableArrays._list, &array);
}

Error JSONObjectPool_ReturnString(JSONObjectPool* self, GenericBuffer* stringBuffer)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (stringBuffer == NULL)
    {
        return CreateNullArgumentError(u8"stringBuffer");
    }
    if (stringBuffer->_userData != self)
    {
        return CreatePoolMismatchError();
    }
    if (!GenericBuffer_Clear(stringBuffer))
    {
        return CreateCapacityError(u8"clear returned JSON string");
    }

    return IList_AddLast(&self->_availableStrings._list, &stringBuffer);
}

Error JSONObjectPool_ReturnValue(JSONObjectPool* self, JSONObjectValue* value)
{
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (value == NULL)
    {
        return CreateNullArgumentError(u8"value");
    }

    switch (value->Type)
    {
        case JSONValueType_String:
            Result = JSONObjectPool_ReturnString(self, value->Value.String);
            break;
        case JSONValueType_Compound:
            Result = JSONObjectPool_ReturnCompound(self, value->Value.Compound, true);
            break;
        case JSONValueType_Array:
            Result = JSONObjectPool_ReturnArray(self, value->Value.Array, true);
            break;
        case JSONValueType_Boolean:
        case JSONValueType_Integer:
        case JSONValueType_RealNumber:
        case JSONValueType_Null:
        case JSONValueType_None:
            Result = Error_CreateSuccess();
            break;
        default:
            Result = CreateInvalidTypeError(value->Type);
            break;
    }

    if (Result.Code == ErrorCode_Success)
    {
        *value = (JSONObjectValue){ .Type = JSONValueType_None };
    }

    return Result;
}
