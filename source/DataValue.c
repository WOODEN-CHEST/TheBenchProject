#include <inttypes.h>
#include <stdio.h>
#include "DataValueInternal.h"
#include "GameCalendar.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"


// Macros.
/** The first capacity a list or compound grows to. */
#define DATA_CONTAINER_FIRST_CAPACITY 4U
/** Bytes counted for values (see DataValue_GetByteSize). Every value counts at least one byte, so the data limits also
 *  bound how many values there are: an empty string counts its end, a list or compound its own byte. */
#define DATA_SIZE_BOOL ((size_t)1)
#define DATA_SIZE_EIGHT_BYTES ((size_t)8)
#define DATA_SIZE_VECTOR3 ((size_t)24)
#define DATA_SIZE_COLOR ((size_t)4)
#define DATA_SIZE_TYPE_BYTE ((size_t)1)
#define DATA_SIZE_STRING_END ((size_t)1)
#define DATA_SIZE_CONTAINER ((size_t)1)
/** How many bytes of a name a message quotes, and the printable ASCII range it keeps. */
#define DATA_PATH_QUOTE_LIMIT ((size_t)64)
#define DATA_PATH_PRINTABLE_FIRST 0x20U
#define DATA_PATH_PRINTABLE_LAST 0x7EU
/** Room for the problem text of a budget a value does not fit. */
#define DATA_BUDGET_PROBLEM_CAPACITY ((size_t)64)


// Fields.
static const unsigned char* const TypeNames[DATA_TYPE_COUNT] =
{
    u8"tag", u8"bool", u8"int", u8"float", u8"string", u8"key", u8"object", u8"vector3", u8"color", u8"date_time",
    u8"list", u8"compound",
};


// Static functions: helpers.
static bool IsKnownType(DataType type)
{
    return ((int)type >= 0) && ((int)type < DATA_TYPE_COUNT);
}

static bool IsFinite(double value)
{
    return !Math_IsNaNDouble(value) && !Math_IsInfinityDouble(value);
}

/* Byte order of two names, the shorter first when one is a prefix of the other. */
static int CompareNames(const unsigned char* a, const unsigned char* b)
{
    size_t Index = 0U;
    while ((a[Index] == b[Index]) && (a[Index] != (unsigned char)0))
    {
        Index++;
    }
    return (int)a[Index] - (int)b[Index];
}

/* The index of the entry named @p name, or where it would go (*outIsFound false). */
static size_t FindEntryIndex(const DataCompound* compound, const unsigned char* name, bool* outIsFound)
{
    size_t Low = 0U;
    size_t High = compound->_count;
    while (Low < High)
    {
        size_t Middle = Low + ((High - Low) / 2U);
        int Order = CompareNames(compound->_entries[Middle].Name, name);
        if (Order == 0)
        {
            *outIsFound = true;
            return Middle;
        }
        if (Order < 0)
        {
            Low = Middle + 1U;
        }
        else
        {
            High = Middle;
        }
    }
    *outIsFound = false;
    return Low;
}

static size_t GetListBytes(size_t capacity)
{
    return sizeof(DataList) + (capacity * sizeof(DataValue));
}

static size_t GetCompoundBytes(size_t capacity)
{
    return sizeof(DataCompound) + (capacity * sizeof(DataCompoundEntry));
}

/* Recomputes a list's depth from its elements (after a removal). */
static void RefreshListDepth(DataList* list)
{
    size_t Deepest = 0U;
    for (uint32_t Index = 0U; Index < list->_count; Index++)
    {
        size_t Depth = DataValue_GetDepth(&list->_items[Index]);
        Deepest = (Depth > Deepest) ? Depth : Deepest;
    }
    list->_depth = (uint32_t)(Deepest + 1U);
}

/* Recomputes a compound's depth from its values (after a replacement or a removal). */
static void RefreshCompoundDepth(DataCompound* compound)
{
    size_t Deepest = 0U;
    for (uint32_t Index = 0U; Index < compound->_count; Index++)
    {
        size_t Depth = DataValue_GetDepth(&compound->_entries[Index].Value);
        Deepest = (Depth > Deepest) ? Depth : Deepest;
    }
    compound->_depth = (uint32_t)(Deepest + 1U);
}

static unsigned char* CloneName(const unsigned char* name)
{
    size_t ByteCount = StringUTF8_GetByteLength(name) + 1U;
    unsigned char* Copy = Memory_Allocate(ByteCount);
    Memory_Copy(name, Copy, ByteCount);
    return Copy;
}


// Static functions: deep copies.
static DataList* CloneList(const DataList* source)
{
    DataList* Copy = Memory_Allocate(GetListBytes(source->_count));
    Copy->_elementType = source->_elementType;
    Copy->_count = source->_count;
    Copy->_capacity = source->_count;
    Copy->_depth = source->_depth;
    for (uint32_t Index = 0U; Index < source->_count; Index++)
    {
        Copy->_items[Index] = DataValue_Clone(&source->_items[Index]);
    }
    return Copy;
}

static DataCompound* CloneCompound(const DataCompound* source)
{
    DataCompound* Copy = Memory_Allocate(GetCompoundBytes(source->_count));
    Copy->_count = source->_count;
    Copy->_capacity = source->_count;
    Copy->_depth = source->_depth;
    for (uint32_t Index = 0U; Index < source->_count; Index++)
    {
        Copy->_entries[Index].Name = CloneName(source->_entries[Index].Name);
        Copy->_entries[Index].Value = DataValue_Clone(&source->_entries[Index].Value);
    }
    return Copy;
}


// Static functions: equality and size.
static bool AreListsEqual(const DataList* a, const DataList* b)
{
    if ((a->_elementType != b->_elementType) || (a->_count != b->_count))
    {
        return false;
    }
    for (uint32_t Index = 0U; Index < a->_count; Index++)
    {
        if (!DataValue_Equals(&a->_items[Index], &b->_items[Index]))
        {
            return false;
        }
    }
    return true;
}

static bool AreCompoundsEqual(const DataCompound* a, const DataCompound* b)
{
    if (a->_count != b->_count)
    {
        return false;
    }
    for (uint32_t Index = 0U; Index < a->_count; Index++)
    {
        if ((CompareNames(a->_entries[Index].Name, b->_entries[Index].Name) != 0)
            || !DataValue_Equals(&a->_entries[Index].Value, &b->_entries[Index].Value))
        {
            return false;
        }
    }
    return true;
}

static size_t GetKeyByteSize(uint32_t key, const KeyRegistry* keys)
{
    const unsigned char* Text = (keys != NULL) ? KeyRegistry_GetText(keys, key) : NULL;
    return (Text != NULL) ? StringUTF8_GetByteLength(Text) : NAMESPACED_KEY_MAX_LENGTH;
}


// Static functions: validation.
static Error ValidateList(const DataList* list, const KeyRegistry* keys, DataPath* path)
{
    if (!IsKnownType(list->_elementType) || (list->_elementType == DataType_Tag))
    {
        return DataPath_MakeError(path, ErrorCode_ArgumentOutOfRange, "a list's elements must have a type other than tag");
    }
    if (list->_count > DATA_VALUE_MAX_ELEMENT_COUNT)
    {
        return DataPath_MakeError(path, ErrorCode_ArgumentOutOfRange, "a list holds at most 4096 elements");
    }
    for (uint32_t Index = 0U; Index < list->_count; Index++)
    {
        DataPath_PushIndex(path, Index);
        Error Result = Error_CreateSuccess();
        if (list->_items[Index].Type != list->_elementType)
        {
            char Problem[96];
            snprintf(Problem, sizeof(Problem), "the element is a %s in a list of %s",
                (const char*)DataType_GetName(list->_items[Index].Type), (const char*)DataType_GetName(list->_elementType));
            Result = DataPath_MakeError(path, ErrorCode_ArgumentOutOfRange, Problem);
        }
        else
        {
            Result = DataValueInternal_Validate(&list->_items[Index], keys, path);
        }
        DataPath_Pop(path);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}

static Error ValidateCompound(const DataCompound* compound, const KeyRegistry* keys, DataPath* path)
{
    if (compound->_count > DATA_VALUE_MAX_ELEMENT_COUNT)
    {
        return DataPath_MakeError(path, ErrorCode_ArgumentOutOfRange, "a compound holds at most 4096 entries");
    }
    for (uint32_t Index = 0U; Index < compound->_count; Index++)
    {
        const DataCompoundEntry* Entry = &compound->_entries[Index];
        size_t NameLength = (Entry->Name != NULL) ? StringUTF8_GetByteLength(Entry->Name) : 0U;
        if ((Entry->Name == NULL) || !DataValueInternal_IsNameValid(Entry->Name, NameLength))
        {
            return DataPath_MakeError(path, ErrorCode_ArgumentOutOfRange, "an entry has a name that breaks the rules");
        }
        DataPath_PushName(path, Entry->Name, NameLength);
        Error Result = Error_CreateSuccess();
        if (Entry->Value.Type == DataType_Tag)
        {
            Result = DataPath_MakeError(path, ErrorCode_ArgumentOutOfRange, "a compound entry must have a value");
        }
        else if (DataValueInternal_IsTypeEntryName(Entry->Name, NameLength) && (Entry->Value.Type != DataType_Key))
        {
            Result = DataPath_MakeError(path, ErrorCode_ArgumentOutOfRange, "\"$type\" must be a key");
        }
        else
        {
            Result = DataValueInternal_Validate(&Entry->Value, keys, path);
        }
        DataPath_Pop(path);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}

static Error ValidateString(const unsigned char* text, DataPath* path)
{
    if (text == NULL)
    {
        return DataPath_MakeError(path, ErrorCode_ArgumentOutOfRange, "the string is missing");
    }
    if (StringUTF8_GetByteLength(text) > DATA_VALUE_MAX_STRING_LENGTH)
    {
        return DataPath_MakeError(path, ErrorCode_ArgumentOutOfRange, "a string holds at most 4096 bytes");
    }
    return StringUTF8_IsEncodingValid(text) ? Error_CreateSuccess()
        : DataPath_MakeError(path, ErrorCode_ArgumentOutOfRange, "the string is not valid UTF-8");
}


// Internal functions (DataValueInternal.h): paths.
void DataPath_PushName(DataPath* self, const unsigned char* name, size_t nameLength)
{
    if (self->Count < DATA_PATH_CAPACITY)
    {
        self->Segments[self->Count] = (DataPathSegment){ .Name = name, .NameLength = nameLength, .Index = 0U };
    }
    self->Count++;
}

void DataPath_PushIndex(DataPath* self, size_t index)
{
    if (self->Count < DATA_PATH_CAPACITY)
    {
        self->Segments[self->Count] = (DataPathSegment){ .Name = NULL, .NameLength = 0U, .Index = index };
    }
    self->Count++;
}

/* Appends the path's text: names joined by '.', indices as [i]. A failed append only shortens the message. */
static void AppendPathText(const DataPath* self, GenericBuffer* text)
{
    size_t Recorded = (self->Count < DATA_PATH_CAPACITY) ? self->Count : DATA_PATH_CAPACITY;
    for (size_t Index = 0U; Index < Recorded; Index++)
    {
        const DataPathSegment* Segment = &self->Segments[Index];
        if (Segment->Name == NULL)
        {
            char Number[32];
            snprintf(Number, sizeof(Number), "[%" PRIu64 "]", (uint64_t)Segment->Index);
            GenericBuffer_AppendString(text, (const unsigned char*)Number);
            continue;
        }
        if (Index > 0U)
        {
            GenericBuffer_AppendByte(text, (unsigned char)'.');
        }
        size_t Count = (Segment->NameLength < DATA_PATH_QUOTE_LIMIT) ? Segment->NameLength : DATA_PATH_QUOTE_LIMIT;
        for (size_t Byte = 0U; Byte < Count; Byte++)
        {
            unsigned char Value = Segment->Name[Byte];
            bool IsPrintable = (Value >= DATA_PATH_PRINTABLE_FIRST) && (Value <= DATA_PATH_PRINTABLE_LAST);
            GenericBuffer_AppendByte(text, IsPrintable ? Value : (unsigned char)'?');
        }
        if (Count < Segment->NameLength)
        {
            GenericBuffer_AppendString(text, (const unsigned char*)u8"...");
        }
    }
    if (self->Count > DATA_PATH_CAPACITY)
    {
        GenericBuffer_AppendString(text, (const unsigned char*)u8"...");
    }
}

static Error MakePathError(const DataPath* self, ErrorCode code, const char* problem, const char* ending)
{
    if (self->Count == 0U)
    {
        return Error_Construct4(code, "%s%s", problem, ending);
    }
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, 0U, sizeof(unsigned char));
    AppendPathText(self, &Text);
    GenericBuffer_NullTerminate(&Text);
    Error Result = Error_Construct4(code, "%s: %s%s", (Text._data != NULL) ? (const char*)Text._data : "?", problem,
        ending);
    Memory_Free(Text._data);
    return Result;
}

Error DataPath_MakeError(const DataPath* self, ErrorCode code, const char* problem)
{
    return MakePathError(self, code, problem, ".");
}

Error DataPath_WrapError(const DataPath* self, ErrorCode code, Error* inner)
{
    const char* Problem = (inner->Message != NULL) ? (const char*)inner->Message : "invalid value.";
    Error Result = MakePathError(self, code, Problem, "");
    Error_Deconstruct(inner);
    return Result;
}


// Internal functions (DataValueInternal.h): values.
bool DataValueInternal_IsTypeEntryName(const unsigned char* name, size_t byteCount)
{
    size_t TypeLength = StringUTF8_GetByteLength(DATA_VALUE_TYPE_ENTRY_NAME);
    return (byteCount == TypeLength) && Memory_IsEqual(name, DATA_VALUE_TYPE_ENTRY_NAME, TypeLength);
}

bool DataValueInternal_IsNameValid(const unsigned char* bytes, size_t byteCount)
{
    if ((bytes == NULL) || (byteCount == 0U) || (byteCount > DATA_VALUE_MAX_NAME_LENGTH))
    {
        return false;
    }
    if ((bytes[0] == (unsigned char)'$') && !DataValueInternal_IsTypeEntryName(bytes, byteCount))
    {
        return false;
    }
    // StringUTF8_IsEncodingValid wants a terminated string: check a terminated copy (no NUL inside it either).
    unsigned char Copy[DATA_VALUE_MAX_NAME_LENGTH + 1U];
    Memory_Copy(bytes, Copy, byteCount);
    Copy[byteCount] = (unsigned char)0;
    return (StringUTF8_GetByteLength(Copy) == byteCount) && StringUTF8_IsEncodingValid(Copy);
}

Error DataValueInternal_Validate(const DataValue* value, const KeyRegistry* keys, DataPath* path)
{
    switch (value->Type)
    {
        case DataType_Tag:
        case DataType_Bool:
        case DataType_Int:
        case DataType_Object:
        case DataType_Color:
            return Error_CreateSuccess();
        case DataType_Float:
            return IsFinite(value->FloatValue) ? Error_CreateSuccess()
                : DataPath_MakeError(path, ErrorCode_ArgumentOutOfRange, "the float must be finite");
        case DataType_String:
            return ValidateString(value->StringValue, path);
        case DataType_Key:
            return ((keys == NULL) || (KeyRegistry_GetText(keys, value->KeyValue) != NULL)) ? Error_CreateSuccess()
                : DataPath_MakeError(path, ErrorCode_ArgumentOutOfRange, "the key is not an id of the key registry");
        case DataType_Vector3:
            return Vector3Double_IsFinite(value->Vector3Value) ? Error_CreateSuccess()
                : DataPath_MakeError(path, ErrorCode_ArgumentOutOfRange, "every component of the vector3 must be finite");
        case DataType_DateTime:
            return GameCalendar_IsDateTimeValid(value->DateTimeValue) ? Error_CreateSuccess()
                : DataPath_MakeError(path, ErrorCode_ArgumentOutOfRange,
                    "the date_time must lie in the calendar (years 1 to 9999)");
        case DataType_List:
            return (value->ListValue != NULL) ? ValidateList(value->ListValue, keys, path)
                : DataPath_MakeError(path, ErrorCode_ArgumentOutOfRange, "the list is missing");
        case DataType_Compound:
            return (value->CompoundValue != NULL) ? ValidateCompound(value->CompoundValue, keys, path)
                : DataPath_MakeError(path, ErrorCode_ArgumentOutOfRange, "the compound is missing");
    }
    return DataPath_MakeError(path, ErrorCode_ArgumentOutOfRange, "the value has an unknown type");
}

Error DataValueInternal_Charge(DataBudget* budget, size_t bytes, const DataPath* path, ErrorCode failureCode)
{
    if (budget == NULL)
    {
        return Error_CreateSuccess();
    }
    if (bytes > budget->Remaining)
    {
        budget->Remaining = 0U;
        char Problem[DATA_BUDGET_PROBLEM_CAPACITY];
        snprintf(Problem, sizeof(Problem), "the data takes at most %" PRIu64 " bytes", (uint64_t)budget->Limit);
        return DataPath_MakeError(path, failureCode, Problem);
    }
    budget->Remaining -= bytes;
    return Error_CreateSuccess();
}

Error DataValueInternal_CheckRead(const DataValue* value, const KeyRegistry* keys, DataPath* path, ErrorCode failureCode)
{
    Error Result = DataValueInternal_Validate(value, keys, path);
    if (Result.Code == ErrorCode_Success)
    {
        return Result;
    }
    Error Recoded = Error_Construct4(failureCode, "%s", (Result.Message != NULL) ? (const char*)Result.Message
        : "invalid value.");
    Error_Deconstruct(&Result);
    return Recoded;
}

CollectionEnumerator* DataValueInternal_BeginEnumeration(ICollection* collection, DataEnumeratorStorage* storage)
{
    if (collection == NULL)
    {
        return NULL;
    }
    if (ICollection_GetEnumeratorSize(collection) <= sizeof(storage->Bytes))
    {
        return ICollection_InitEnumerator(collection, storage->Bytes);
    }
    return ICollection_CreateEnumerator(collection);
}

void DataValueInternal_EndEnumeration(CollectionEnumerator* enumerator, const DataEnumeratorStorage* storage)
{
    if (enumerator == NULL)
    {
        return;
    }
    if ((const void*)enumerator == (const void*)storage->Bytes)
    {
        CollectionEnumerator_Deconstruct(enumerator);
        return;
    }
    CollectionEnumerator_Destroy(enumerator);
}


// Public functions: types.
const unsigned char* DataType_GetName(DataType type)
{
    return IsKnownType(type) ? TypeNames[type] : u8"unknown";
}

bool DataType_TryParseName(const unsigned char* name, size_t byteCount, DataType* outType)
{
    if ((name == NULL) || (outType == NULL))
    {
        return false;
    }
    for (int Type = 0; Type < DATA_TYPE_COUNT; Type++)
    {
        const unsigned char* Candidate = TypeNames[Type];
        if ((StringUTF8_GetByteLength(Candidate) == byteCount) && Memory_IsEqual(Candidate, name, byteCount))
        {
            *outType = (DataType)Type;
            return true;
        }
    }
    return false;
}


// Public functions: values that own memory.
Error DataValue_CreateString(const unsigned char* text, DataValue* outValue)
{
    if ((text == NULL) || (outValue == NULL))
    {
        if (outValue != NULL)
        {
            *outValue = DataValue_CreateTag();
        }
        return Error_Construct2(ErrorCode_IllegalArgument, "DataValue_CreateString: text and outValue must not be NULL.");
    }
    size_t Length = 0U;
    while ((text[Length] != (unsigned char)0) && (Length <= DATA_VALUE_MAX_STRING_LENGTH))
    {
        Length++;
    }
    return DataValue_CreateStringFromBytes(text, Length, outValue);
}

Error DataValue_CreateStringFromBytes(const unsigned char* bytes, size_t byteCount, DataValue* outValue)
{
    if (outValue == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "DataValue_CreateStringFromBytes: outValue must not be NULL.");
    }
    *outValue = DataValue_CreateTag();
    if (((bytes == NULL) && (byteCount > 0U)) || (byteCount > DATA_VALUE_MAX_STRING_LENGTH))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "A string holds at most 4096 bytes.");
    }
    unsigned char* Text = Memory_Allocate(byteCount + 1U);
    if (byteCount > 0U)
    {
        Memory_Copy(bytes, Text, byteCount);
    }
    Text[byteCount] = (unsigned char)0;
    if (StringUTF8_GetByteLength(Text) != byteCount)
    {
        Memory_Free(Text);
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "A string must not contain NUL characters.");
    }
    if (!StringUTF8_IsEncodingValid(Text))
    {
        Memory_Free(Text);
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "A string must be valid UTF-8.");
    }
    *outValue = (DataValue){ .Type = DataType_String, .StringValue = Text };
    return Error_CreateSuccess();
}

Error DataValue_CreateList(DataType elementType, DataValue* outValue)
{
    if (outValue == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "DataValue_CreateList: outValue must not be NULL.");
    }
    *outValue = DataValue_CreateTag();
    if (!IsKnownType(elementType) || (elementType == DataType_Tag))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "DataValue_CreateList: the element type must be a data type other than tag.");
    }
    DataList* List = Memory_Allocate(GetListBytes(0U));
    *List = (DataList){ ._elementType = elementType, ._count = 0U, ._capacity = 0U, ._depth = 1U };
    *outValue = (DataValue){ .Type = DataType_List, .ListValue = List };
    return Error_CreateSuccess();
}

Error DataValue_CreateCompound(DataValue* outValue)
{
    if (outValue == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "DataValue_CreateCompound: outValue must not be NULL.");
    }
    DataCompound* Compound = Memory_Allocate(GetCompoundBytes(0U));
    *Compound = (DataCompound){ ._count = 0U, ._capacity = 0U, ._depth = 1U };
    *outValue = (DataValue){ .Type = DataType_Compound, .CompoundValue = Compound };
    return Error_CreateSuccess();
}

DataValue DataValue_Clone(const DataValue* source)
{
    if (source == NULL)
    {
        return DataValue_CreateTag();
    }
    DataValue Copy = *source;
    switch (source->Type)
    {
        case DataType_String:
            Copy.StringValue = (source->StringValue != NULL) ? CloneName(source->StringValue) : NULL;
            break;
        case DataType_List:
            Copy.ListValue = (source->ListValue != NULL) ? CloneList(source->ListValue) : NULL;
            break;
        case DataType_Compound:
            Copy.CompoundValue = (source->CompoundValue != NULL) ? CloneCompound(source->CompoundValue) : NULL;
            break;
        case DataType_Tag:
        case DataType_Bool:
        case DataType_Int:
        case DataType_Float:
        case DataType_Key:
        case DataType_Object:
        case DataType_Vector3:
        case DataType_Color:
        case DataType_DateTime:
            break;
    }
    return Copy;
}

void DataValue_Deconstruct(DataValue* value)
{
    if (value == NULL)
    {
        return;
    }
    switch (value->Type)
    {
        case DataType_String:
            Memory_Free(value->StringValue);
            break;
        case DataType_List:
            if (value->ListValue != NULL)
            {
                for (uint32_t Index = 0U; Index < value->ListValue->_count; Index++)
                {
                    DataValue_Deconstruct(&value->ListValue->_items[Index]);
                }
                Memory_Free(value->ListValue);
            }
            break;
        case DataType_Compound:
            if (value->CompoundValue != NULL)
            {
                for (uint32_t Index = 0U; Index < value->CompoundValue->_count; Index++)
                {
                    Memory_Free(value->CompoundValue->_entries[Index].Name);
                    DataValue_Deconstruct(&value->CompoundValue->_entries[Index].Value);
                }
                Memory_Free(value->CompoundValue);
            }
            break;
        case DataType_Tag:
        case DataType_Bool:
        case DataType_Int:
        case DataType_Float:
        case DataType_Key:
        case DataType_Object:
        case DataType_Vector3:
        case DataType_Color:
        case DataType_DateTime:
            break;
    }
    *value = DataValue_CreateTag();
}


// Public functions: questions.
bool DataValue_Equals(const DataValue* a, const DataValue* b)
{
    if ((a == NULL) || (b == NULL) || (a->Type != b->Type))
    {
        return false;
    }
    switch (a->Type)
    {
        case DataType_Tag:
            return true;
        case DataType_Bool:
            return a->BoolValue == b->BoolValue;
        case DataType_Int:
            return a->IntValue == b->IntValue;
        case DataType_Float:
            return a->FloatValue == b->FloatValue;
        case DataType_String:
            return (a->StringValue != NULL) && (b->StringValue != NULL)
                && (CompareNames(a->StringValue, b->StringValue) == 0);
        case DataType_Key:
            return a->KeyValue == b->KeyValue;
        case DataType_Object:
            return a->ObjectValue == b->ObjectValue;
        case DataType_Vector3:
            return (a->Vector3Value.X == b->Vector3Value.X) && (a->Vector3Value.Y == b->Vector3Value.Y)
                && (a->Vector3Value.Z == b->Vector3Value.Z);
        case DataType_Color:
            return (a->ColorValue.r == b->ColorValue.r) && (a->ColorValue.g == b->ColorValue.g)
                && (a->ColorValue.b == b->ColorValue.b) && (a->ColorValue.a == b->ColorValue.a);
        case DataType_DateTime:
            return a->DateTimeValue == b->DateTimeValue;
        case DataType_List:
            return (a->ListValue != NULL) && (b->ListValue != NULL) && AreListsEqual(a->ListValue, b->ListValue);
        case DataType_Compound:
            return (a->CompoundValue != NULL) && (b->CompoundValue != NULL)
                && AreCompoundsEqual(a->CompoundValue, b->CompoundValue);
    }
    return false;
}

size_t DataValue_GetDepth(const DataValue* value)
{
    if (value == NULL)
    {
        return 0U;
    }
    if ((value->Type == DataType_List) && (value->ListValue != NULL))
    {
        return value->ListValue->_depth;
    }
    if ((value->Type == DataType_Compound) && (value->CompoundValue != NULL))
    {
        return value->CompoundValue->_depth;
    }
    return 1U;
}

size_t DataValue_GetByteSize(const DataValue* value, const KeyRegistry* keys)
{
    if (value == NULL)
    {
        return 0U;
    }
    size_t Size = 0U;
    switch (value->Type)
    {
        case DataType_Tag:
            return 0U;
        case DataType_Bool:
            return DATA_SIZE_BOOL;
        case DataType_Int:
        case DataType_Float:
        case DataType_Object:
        case DataType_DateTime:
            return DATA_SIZE_EIGHT_BYTES;
        case DataType_Vector3:
            return DATA_SIZE_VECTOR3;
        case DataType_Color:
            return DATA_SIZE_COLOR;
        case DataType_String:
            return ((value->StringValue != NULL) ? StringUTF8_GetByteLength(value->StringValue) : 0U)
                + DATA_SIZE_STRING_END;
        case DataType_Key:
            return GetKeyByteSize(value->KeyValue, keys);
        case DataType_List:
            Size = DATA_SIZE_CONTAINER;
            for (uint32_t Index = 0U; (value->ListValue != NULL) && (Index < value->ListValue->_count); Index++)
            {
                Size += DataValue_GetByteSize(&value->ListValue->_items[Index], keys);
            }
            return Size;
        case DataType_Compound:
            Size = DATA_SIZE_CONTAINER;
            for (uint32_t Index = 0U; (value->CompoundValue != NULL) && (Index < value->CompoundValue->_count); Index++)
            {
                const DataCompoundEntry* Entry = &value->CompoundValue->_entries[Index];
                Size += StringUTF8_GetByteLength(Entry->Name) + DATA_SIZE_TYPE_BYTE
                    + DataValue_GetByteSize(&Entry->Value, keys);
            }
            return Size;
    }
    return 0U;
}

Error DataValue_Validate(const DataValue* value, const KeyRegistry* keys)
{
    if (value == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "DataValue_Validate: value must not be NULL.");
    }
    DataPath Path = DataPath_Create();
    if (DataValue_GetDepth(value) > DATA_VALUE_MAX_DEPTH)
    {
        return DataPath_MakeError(&Path, ErrorCode_ArgumentOutOfRange, "a value nests at most 16 levels deep");
    }
    return DataValueInternal_Validate(value, keys, &Path);
}


// Public functions: lists.
DataType DataValue_GetListElementType(const DataValue* list)
{
    return ((list != NULL) && (list->Type == DataType_List) && (list->ListValue != NULL)) ? list->ListValue->_elementType
        : DataType_Tag;
}

size_t DataValue_GetListCount(const DataValue* list)
{
    return ((list != NULL) && (list->Type == DataType_List) && (list->ListValue != NULL)) ? list->ListValue->_count
        : 0U;
}

const DataValue* DataValue_GetListElement(const DataValue* list, size_t index)
{
    if (index >= DataValue_GetListCount(list))
    {
        return NULL;
    }
    return &list->ListValue->_items[index];
}

Error DataValue_ListAdd(DataValue* list, DataValue* element)
{
    if ((list == NULL) || (element == NULL) || (list->Type != DataType_List) || (list->ListValue == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "DataValue_ListAdd: list must be a list and element not NULL.");
    }
    DataList* List = list->ListValue;
    if (element->Type != List->_elementType)
    {
        return Error_Construct4(ErrorCode_IllegalArgument, "DataValue_ListAdd: the list holds %s values, not %s.",
            (const char*)DataType_GetName(List->_elementType), (const char*)DataType_GetName(element->Type));
    }
    if (List->_count >= DATA_VALUE_MAX_ELEMENT_COUNT)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "DataValue_ListAdd: a list holds at most 4096 elements.");
    }
    size_t ElementDepth = DataValue_GetDepth(element);
    if ((ElementDepth + 1U) > DATA_VALUE_MAX_DEPTH)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "DataValue_ListAdd: a value nests at most 16 levels deep.");
    }

    if (List->_count == List->_capacity)
    {
        uint32_t Capacity = (List->_capacity == 0U) ? DATA_CONTAINER_FIRST_CAPACITY : (List->_capacity * 2U);
        Capacity = (Capacity > DATA_VALUE_MAX_ELEMENT_COUNT) ? (uint32_t)DATA_VALUE_MAX_ELEMENT_COUNT : Capacity;
        List = Memory_Reallocate(List, GetListBytes(Capacity));
        List->_capacity = Capacity;
        list->ListValue = List;
    }
    List->_items[List->_count++] = *element;
    if ((ElementDepth + 1U) > List->_depth)
    {
        List->_depth = (uint32_t)(ElementDepth + 1U);
    }
    *element = DataValue_CreateTag();
    return Error_CreateSuccess();
}

Error DataValue_ListRemoveAt(DataValue* list, size_t index)
{
    if ((list == NULL) || (list->Type != DataType_List) || (list->ListValue == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "DataValue_ListRemoveAt: list must be a list.");
    }
    DataList* List = list->ListValue;
    if (index >= List->_count)
    {
        return Error_Construct2(ErrorCode_IndexOutOfBounds, "DataValue_ListRemoveAt: index out of range.");
    }
    DataValue_Deconstruct(&List->_items[index]);
    size_t After = List->_count - index - 1U;
    if (After > 0U)
    {
        Memory_Move(&List->_items[index + 1U], &List->_items[index], After * sizeof(DataValue));
    }
    List->_count--;
    RefreshListDepth(List);
    return Error_CreateSuccess();
}


// Public functions: compounds.
size_t DataValue_GetCompoundCount(const DataValue* compound)
{
    return ((compound != NULL) && (compound->Type == DataType_Compound) && (compound->CompoundValue != NULL))
        ? compound->CompoundValue->_count : 0U;
}

const unsigned char* DataValue_GetCompoundEntryName(const DataValue* compound, size_t index)
{
    if (index >= DataValue_GetCompoundCount(compound))
    {
        return NULL;
    }
    return compound->CompoundValue->_entries[index].Name;
}

const DataValue* DataValue_GetCompoundEntryValue(const DataValue* compound, size_t index)
{
    if (index >= DataValue_GetCompoundCount(compound))
    {
        return NULL;
    }
    return &compound->CompoundValue->_entries[index].Value;
}

const DataValue* DataValue_FindCompoundEntry(const DataValue* compound, const unsigned char* name)
{
    if ((name == NULL) || (DataValue_GetCompoundCount(compound) == 0U))
    {
        return NULL;
    }
    bool IsFound = false;
    size_t Index = FindEntryIndex(compound->CompoundValue, name, &IsFound);
    return IsFound ? &compound->CompoundValue->_entries[Index].Value : NULL;
}

Error DataValue_CompoundSet(DataValue* compound, const unsigned char* name, DataValue* value)
{
    if ((compound == NULL) || (name == NULL) || (value == NULL) || (compound->Type != DataType_Compound)
        || (compound->CompoundValue == NULL) || (value->Type == DataType_Tag))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "DataValue_CompoundSet: compound must be a compound, name not NULL and value a value (not a tag).");
    }
    size_t NameLength = 0U;
    while ((name[NameLength] != (unsigned char)0) && (NameLength <= DATA_VALUE_MAX_NAME_LENGTH))
    {
        NameLength++;
    }
    if (!DataValueInternal_IsNameValid(name, NameLength))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "DataValue_CompoundSet: a name is 1 to 256 bytes of UTF-8 and starts with '$' only as \"$type\".");
    }
    if (DataValueInternal_IsTypeEntryName(name, NameLength) && (value->Type != DataType_Key))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "DataValue_CompoundSet: \"$type\" must be a key.");
    }
    size_t ValueDepth = DataValue_GetDepth(value);
    if ((ValueDepth + 1U) > DATA_VALUE_MAX_DEPTH)
    {
        return Error_Construct2(ErrorCode_InvalidOperation,
            "DataValue_CompoundSet: a value nests at most 16 levels deep.");
    }

    DataCompound* Compound = compound->CompoundValue;
    bool IsFound = false;
    size_t Index = FindEntryIndex(Compound, name, &IsFound);
    if (IsFound)
    {
        DataValue_Deconstruct(&Compound->_entries[Index].Value);
        Compound->_entries[Index].Value = *value;
        RefreshCompoundDepth(Compound);
        *value = DataValue_CreateTag();
        return Error_CreateSuccess();
    }
    if (Compound->_count >= DATA_VALUE_MAX_ELEMENT_COUNT)
    {
        return Error_Construct2(ErrorCode_InvalidOperation,
            "DataValue_CompoundSet: a compound holds at most 4096 entries.");
    }

    if (Compound->_count == Compound->_capacity)
    {
        uint32_t Capacity = (Compound->_capacity == 0U) ? DATA_CONTAINER_FIRST_CAPACITY : (Compound->_capacity * 2U);
        Capacity = (Capacity > DATA_VALUE_MAX_ELEMENT_COUNT) ? (uint32_t)DATA_VALUE_MAX_ELEMENT_COUNT : Capacity;
        Compound = Memory_Reallocate(Compound, GetCompoundBytes(Capacity));
        Compound->_capacity = Capacity;
        compound->CompoundValue = Compound;
    }
    size_t After = Compound->_count - Index;
    if (After > 0U)
    {
        Memory_Move(&Compound->_entries[Index], &Compound->_entries[Index + 1U], After * sizeof(DataCompoundEntry));
    }
    Compound->_entries[Index].Name = CloneName(name);
    Compound->_entries[Index].Value = *value;
    Compound->_count++;
    if ((ValueDepth + 1U) > Compound->_depth)
    {
        Compound->_depth = (uint32_t)(ValueDepth + 1U);
    }
    *value = DataValue_CreateTag();
    return Error_CreateSuccess();
}

bool DataValue_CompoundRemove(DataValue* compound, const unsigned char* name)
{
    if ((name == NULL) || (DataValue_GetCompoundCount(compound) == 0U))
    {
        return false;
    }
    DataCompound* Compound = compound->CompoundValue;
    bool IsFound = false;
    size_t Index = FindEntryIndex(Compound, name, &IsFound);
    if (!IsFound)
    {
        return false;
    }
    Memory_Free(Compound->_entries[Index].Name);
    DataValue_Deconstruct(&Compound->_entries[Index].Value);
    size_t After = Compound->_count - Index - 1U;
    if (After > 0U)
    {
        Memory_Move(&Compound->_entries[Index + 1U], &Compound->_entries[Index], After * sizeof(DataCompoundEntry));
    }
    Compound->_count--;
    RefreshCompoundDepth(Compound);
    return true;
}
