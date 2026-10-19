#include <float.h>
#include <inttypes.h>
#include <stdio.h>
#include "PropertyDescriptorInternal.h"
#include "AssetManager.h"
#include "GameCalendar.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"


// Macros.
/** Every flag bit a descriptor may carry. */
#define PROPERTY_KNOWN_FLAGS ((uint32_t)(PropertyFlags_Persistent | PropertyFlags_Editable | PropertyFlags_ReadOnly \
    | PropertyFlags_Script))
/** Room for a rule's text, e.g. "finite and in [-3.40282e+38, 3.40282e+38)". */
#define PROPERTY_RULE_TEXT_SIZE ((size_t)128)
/** The largest value of an enum stored in 1 and in 2 bytes (unsigned). */
#define PROPERTY_ENUM_UINT8_MAX 255
#define PROPERTY_ENUM_UINT16_MAX 65535
/** The asset name grammar, for messages (see AssetManager_IsValidAssetName). */
#define PROPERTY_ASSET_NAME_GRAMMAR "[a-z][a-z0-9_.-]*"


// Static functions: helpers.
/* [a-z][a-z0-9_]*: keys, table names and enum names. */
static bool IsSnakeCase(const unsigned char* text)
{
    if ((text == NULL) || (text[0] < (unsigned char)'a') || (text[0] > (unsigned char)'z'))
    {
        return false;
    }
    for (size_t Index = 1; text[Index] != (unsigned char)0; Index++)
    {
        unsigned char Byte = text[Index];
        bool IsLetter = (Byte >= (unsigned char)'a') && (Byte <= (unsigned char)'z');
        bool IsDigit = (Byte >= (unsigned char)'0') && (Byte <= (unsigned char)'9');
        if (!IsLetter && !IsDigit && (Byte != (unsigned char)'_'))
        {
            return false;
        }
    }
    return true;
}

/* Byte equality; two NULLs are equal, NULL and a string are not. */
static bool AreStringsEqual(const unsigned char* a, const unsigned char* b)
{
    if ((a == NULL) || (b == NULL))
    {
        return a == b;
    }
    size_t Index = 0;
    while ((a[Index] == b[Index]) && (a[Index] != (unsigned char)0))
    {
        Index++;
    }
    return a[Index] == b[Index];
}

/* An owned copy of @p text, or NULL for NULL. Memory_Allocate aborts on failure, so this cannot fail. */
static unsigned char* CloneString(const unsigned char* text)
{
    if (text == NULL)
    {
        return NULL;
    }
    size_t ByteCount = StringUTF8_GetByteLength(text) + 1U;
    unsigned char* Copy = Memory_Allocate(ByteCount);
    Memory_Copy(text, Copy, ByteCount);
    return Copy;
}

static bool IsFloatFinite(float value)
{
    return !Math_IsNaNFloat(value) && !Math_IsInfinityFloat(value);
}

/* The field size a type needs; 0 for Enum, whose storage is 1, 2 or 4 bytes. */
static size_t GetStorageSize(PropertyType type)
{
    switch (type)
    {
        case PropertyType_Bool:
            return sizeof(bool);
        case PropertyType_Int32:
            return sizeof(int32_t);
        case PropertyType_UInt32:
            return sizeof(uint32_t);
        case PropertyType_UInt64:
        case PropertyType_ObjectId:
            return sizeof(uint64_t);
        case PropertyType_Float:
        case PropertyType_Angle:
            return sizeof(float);
        case PropertyType_Double:
            return sizeof(double);
        case PropertyType_Date:
            return sizeof(int64_t);
        case PropertyType_Vector2:
            return sizeof(Vector2);
        case PropertyType_Vector3:
            return sizeof(Vector3);
        case PropertyType_Color:
            return sizeof(Color);
        case PropertyType_RenderColor:
            return sizeof(RenderColor);
        case PropertyType_String:
        case PropertyType_AssetName:
            return sizeof(unsigned char*);
        case PropertyType_Enum:
            return 0U;
    }
    return 0U;
}

/* Types that take Minimum/Maximum rules. */
static bool IsBoundedType(PropertyType type)
{
    return (type == PropertyType_Int32) || (type == PropertyType_UInt32) || (type == PropertyType_Float)
        || (type == PropertyType_Double) || (type == PropertyType_Angle) || (type == PropertyType_Vector2)
        || (type == PropertyType_Vector3);
}

static bool IsValidType(PropertyType type)
{
    return (type == PropertyType_Enum) || (GetStorageSize(type) != 0U);
}

/* Reads the pointer of a string field: the instance's own (owned) string. */
static unsigned char* ReadOwnedString(const PropertyDescriptor* descriptor, const void* instance)
{
    unsigned char* Text = NULL;
    Memory_Copy((const unsigned char*)instance + descriptor->Offset, &Text, sizeof(Text));
    return Text;
}

static int32_t ReadEnumStorage(const unsigned char* field, size_t size)
{
    if (size == sizeof(uint8_t))
    {
        uint8_t Value = 0U;
        Memory_Copy(field, &Value, sizeof(Value));
        return (int32_t)Value;
    }
    if (size == sizeof(uint16_t))
    {
        uint16_t Value = 0U;
        Memory_Copy(field, &Value, sizeof(Value));
        return (int32_t)Value;
    }
    int32_t Value = 0;
    Memory_Copy(field, &Value, sizeof(Value));
    return Value;
}

/* The value must fit the storage (PropertyTable_Check makes sure every table entry does). */
static void WriteEnumStorage(unsigned char* field, size_t size, int32_t value)
{
    if (size == sizeof(uint8_t))
    {
        uint8_t Narrow = (uint8_t)value;
        Memory_Copy(&Narrow, field, sizeof(Narrow));
        return;
    }
    if (size == sizeof(uint16_t))
    {
        uint16_t Narrow = (uint16_t)value;
        Memory_Copy(&Narrow, field, sizeof(Narrow));
        return;
    }
    Memory_Copy(&value, field, sizeof(value));
}


// Static functions: rules.
/* "finite and in [0, 1]", "finite and > 0", "in [0, 10]", "finite" ... */
static void FormatRuleText(const PropertyRules* rules, bool isFiniteRequired, char* buffer, size_t size)
{
    const char* Finite = isFiniteRequired ? "finite" : "";
    const char* Joiner = isFiniteRequired ? " and " : "";
    if (rules->HasMinimum && rules->HasMaximum)
    {
        snprintf(buffer, size, "%s%sin %c%g, %g%c", Finite, Joiner, rules->IsMinimumExclusive ? '(' : '[',
            rules->Minimum, rules->Maximum, rules->IsMaximumExclusive ? ')' : ']');
    }
    else if (rules->HasMinimum)
    {
        snprintf(buffer, size, "%s%s%s %g", Finite, Joiner, rules->IsMinimumExclusive ? ">" : ">=", rules->Minimum);
    }
    else if (rules->HasMaximum)
    {
        snprintf(buffer, size, "%s%s%s %g", Finite, Joiner, rules->IsMaximumExclusive ? "<" : "<=", rules->Maximum);
    }
    else
    {
        snprintf(buffer, size, "%s", isFiniteRequired ? "finite" : "valid");
    }
}

/* "<table>.<key> (<Member>) must <rule>." */
static Error MakeRuleError(const PropertyTable* table, const PropertyDescriptor* descriptor, const char* rule)
{
    return Error_Construct4(ErrorCode_ArgumentOutOfRange, "%s.%s (%s) must %s.", (const char*)table->Name,
        (const char*)descriptor->Key, (descriptor->MemberName != NULL) ? (const char*)descriptor->MemberName : "?",
        rule);
}

static bool IsWithinBounds(const PropertyRules* rules, double value)
{
    if (rules->HasMinimum && (rules->IsMinimumExclusive ? !(value > rules->Minimum) : !(value >= rules->Minimum)))
    {
        return false;
    }
    if (rules->HasMaximum && (rules->IsMaximumExclusive ? !(value < rules->Maximum) : !(value <= rules->Maximum)))
    {
        return false;
    }
    return true;
}

/* Floats compare in float precision, so a bound like 0.1 accepts 0.1f. */
static bool IsFloatWithinBounds(const PropertyRules* rules, float value)
{
    if (rules->HasMinimum)
    {
        float Minimum = (float)rules->Minimum;
        if (rules->IsMinimumExclusive ? !(value > Minimum) : !(value >= Minimum))
        {
            return false;
        }
    }
    if (rules->HasMaximum)
    {
        float Maximum = (float)rules->Maximum;
        if (rules->IsMaximumExclusive ? !(value < Maximum) : !(value <= Maximum))
        {
            return false;
        }
    }
    return true;
}

static Error CheckFloats(const PropertyTable* table, const PropertyDescriptor* descriptor, const float* components,
    size_t count)
{
    for (size_t Index = 0; Index < count; Index++)
    {
        if (!IsFloatFinite(components[Index]) || !IsFloatWithinBounds(&descriptor->Rules, components[Index]))
        {
            char Rule[PROPERTY_RULE_TEXT_SIZE];
            char Text[PROPERTY_RULE_TEXT_SIZE + 32U];
            FormatRuleText(&descriptor->Rules, true, Rule, sizeof(Rule));
            snprintf(Text, sizeof(Text), (count > 1U) ? "have every component %s" : "be %s", Rule);
            return MakeRuleError(table, descriptor, Text);
        }
    }
    return Error_CreateSuccess();
}

static Error CheckDouble(const PropertyTable* table, const PropertyDescriptor* descriptor, double value)
{
    if (!Math_IsNaNDouble(value) && !Math_IsInfinityDouble(value) && IsWithinBounds(&descriptor->Rules, value))
    {
        return Error_CreateSuccess();
    }
    char Rule[PROPERTY_RULE_TEXT_SIZE];
    char Text[PROPERTY_RULE_TEXT_SIZE + 8U];
    FormatRuleText(&descriptor->Rules, true, Rule, sizeof(Rule));
    snprintf(Text, sizeof(Text), "be %s", Rule);
    return MakeRuleError(table, descriptor, Text);
}

static Error CheckInteger(const PropertyTable* table, const PropertyDescriptor* descriptor, int64_t value)
{
    if (IsWithinBounds(&descriptor->Rules, (double)value))
    {
        return Error_CreateSuccess();
    }
    char Rule[PROPERTY_RULE_TEXT_SIZE];
    char Text[PROPERTY_RULE_TEXT_SIZE + 8U];
    FormatRuleText(&descriptor->Rules, false, Rule, sizeof(Rule));
    snprintf(Text, sizeof(Text), "be %s", Rule);
    return MakeRuleError(table, descriptor, Text);
}

static Error CheckString(const PropertyTable* table, const PropertyDescriptor* descriptor, const unsigned char* text)
{
    if (text == NULL)
    {
        return descriptor->Rules.IsNullAllowed ? Error_CreateSuccess() : MakeRuleError(table, descriptor, "not be null");
    }
    size_t ByteCount = StringUTF8_GetByteLength(text);
    if (ByteCount > descriptor->Rules.MaxLength)
    {
        char Text[PROPERTY_RULE_TEXT_SIZE];
        snprintf(Text, sizeof(Text), "be at most %" PRIu64 " bytes long", (uint64_t)descriptor->Rules.MaxLength);
        return MakeRuleError(table, descriptor, Text);
    }
    if (!StringUTF8_IsEncodingValid(text))
    {
        return MakeRuleError(table, descriptor, "be valid UTF-8");
    }
    if ((descriptor->Type == PropertyType_AssetName) && !AssetManager_IsValidAssetName(text))
    {
        return MakeRuleError(table, descriptor, "be an asset name matching " PROPERTY_ASSET_NAME_GRAMMAR);
    }
    return Error_CreateSuccess();
}

/* Lists the entries' names: "one of \"both\", \"near\" or \"far\"" (a failed append only shortens the message). */
static Error CheckEnum(const PropertyTable* table, const PropertyDescriptor* descriptor, int32_t value)
{
    const PropertyEnumTable* Enum = descriptor->EnumTable;
    if ((Enum == NULL) || (PropertyInternal_FindEnumByValue(Enum, value) != NULL))
    {
        return (Enum == NULL) ? MakeRuleError(table, descriptor, "have an enum table") : Error_CreateSuccess();
    }

    GenericBuffer Names;
    GenericBuffer_AllocateVariable(&Names, 0U, sizeof(unsigned char));
    Error AppendResult = StringUTF8_Format(u8"be one of ", &Names);
    if (AppendResult.Code == ErrorCode_Success)
    {
        AppendResult = PropertyInternal_AppendEnumNames(Enum, &Names);
    }
    Error_Deconstruct(&AppendResult);
    Error Result = MakeRuleError(table, descriptor, (Names._count > 0U) ? (const char*)Names._data : "be an enum value");
    Memory_Free(Names._data);
    return Result;
}


// Static functions: the table check.
static Error TableError(const PropertyTable* table, const PropertyDescriptor* descriptor, const char* problem)
{
    const char* TableName = ((table != NULL) && (table->Name != NULL)) ? (const char*)table->Name : "?";
    if (descriptor == NULL)
    {
        return Error_Construct4(ErrorCode_IllegalArgument, "Property table %s: %s.", TableName, problem);
    }
    const char* Key = (descriptor->Key != NULL) ? (const char*)descriptor->Key : "?";
    return Error_Construct4(ErrorCode_IllegalArgument, "Property table %s, %s: %s.", TableName, Key, problem);
}

static bool IsRetired(const PropertyTable* table, uint16_t wireId)
{
    for (size_t Index = 0; Index < table->RetiredWireIdCount; Index++)
    {
        if (table->RetiredWireIds[Index] == wireId)
        {
            return true;
        }
    }
    return false;
}

static Error CheckTableHeader(const PropertyTable* table)
{
    if (!IsSnakeCase(table->Name))
    {
        return TableError(table, NULL, "the name must be snake_case");
    }
    if (table->Version < 1U)
    {
        return TableError(table, NULL, "the version must be at least 1");
    }
    if (table->StructSize == 0U)
    {
        return TableError(table, NULL, "the struct size must not be 0");
    }
    if ((table->Descriptors == NULL) || (table->DescriptorCount == 0U))
    {
        return TableError(table, NULL, "it must have at least one descriptor");
    }
    if ((table->RetiredWireIdCount > 0U) && (table->RetiredWireIds == NULL))
    {
        return TableError(table, NULL, "the retired wire ids are missing");
    }
    for (size_t Index = 0; Index < table->RetiredWireIdCount; Index++)
    {
        if (table->RetiredWireIds[Index] == 0U)
        {
            return TableError(table, NULL, "a retired wire id is 0");
        }
        for (size_t Other = Index + 1U; Other < table->RetiredWireIdCount; Other++)
        {
            if (table->RetiredWireIds[Other] == table->RetiredWireIds[Index])
            {
                return TableError(table, NULL, "a wire id is retired twice");
            }
        }
    }
    return Error_CreateSuccess();
}

/* Key, wire id, type, field placement, flags and version of one descriptor. */
static Error CheckDescriptorShape(const PropertyTable* table, const PropertyDescriptor* descriptor)
{
    if (!IsSnakeCase(descriptor->Key))
    {
        return TableError(table, descriptor, "the key must be snake_case");
    }
    if (descriptor->WireId == 0U)
    {
        return TableError(table, descriptor, "the wire id must not be 0");
    }
    if (IsRetired(table, descriptor->WireId))
    {
        return TableError(table, descriptor, "the wire id is retired");
    }
    if (!IsValidType(descriptor->Type))
    {
        return TableError(table, descriptor, "the type is not a PropertyType");
    }

    size_t ExpectedSize = GetStorageSize(descriptor->Type);
    bool IsSizeValid = (descriptor->Type == PropertyType_Enum)
        ? ((descriptor->Size == 1U) || (descriptor->Size == 2U) || (descriptor->Size == 4U))
        : (descriptor->Size == ExpectedSize);
    if (!IsSizeValid)
    {
        return TableError(table, descriptor, "the field's size does not match its type");
    }
    if ((descriptor->Offset > table->StructSize) || (descriptor->Size > (table->StructSize - descriptor->Offset)))
    {
        return TableError(table, descriptor, "the field lies outside the struct");
    }
    if ((descriptor->Flags & ~PROPERTY_KNOWN_FLAGS) != 0U)
    {
        return TableError(table, descriptor, "it has unknown flag bits");
    }
    if ((descriptor->SinceVersion < 1U) || (descriptor->SinceVersion > table->Version))
    {
        return TableError(table, descriptor, "SinceVersion must be in [1, the table's version]");
    }
    return Error_CreateSuccess();
}

/* Whether a bound fits the type: finite, within float range for floats, whole and in range for integers. */
static bool IsBoundValid(PropertyType type, double bound)
{
    if (Math_IsNaNDouble(bound) || Math_IsInfinityDouble(bound))
    {
        return false;
    }
    if (type == PropertyType_Double)
    {
        return true;
    }
    if (type == PropertyType_Int32)
    {
        return (Math_TruncateDouble(bound) == bound) && (bound >= (double)INT32_MIN) && (bound <= (double)INT32_MAX);
    }
    if (type == PropertyType_UInt32)
    {
        return (Math_TruncateDouble(bound) == bound) && (bound >= 0.0) && (bound <= (double)UINT32_MAX);
    }
    return Math_AbsDouble(bound) <= (double)FLT_MAX;
}

static Error CheckDescriptorRules(const PropertyTable* table, const PropertyDescriptor* descriptor)
{
    const PropertyRules* Rules = &descriptor->Rules;
    bool HasBounds = Rules->HasMinimum || Rules->HasMaximum;
    if (HasBounds && !IsBoundedType(descriptor->Type))
    {
        return TableError(table, descriptor, "its type takes no minimum or maximum");
    }
    if ((Rules->HasMinimum && !IsBoundValid(descriptor->Type, Rules->Minimum))
        || (Rules->HasMaximum && !IsBoundValid(descriptor->Type, Rules->Maximum)))
    {
        return TableError(table, descriptor, "a bound is not finite, not whole or out of the type's range");
    }
    if (Rules->HasMinimum && Rules->HasMaximum)
    {
        bool IsEitherExclusive = Rules->IsMinimumExclusive || Rules->IsMaximumExclusive;
        if ((Rules->Minimum > Rules->Maximum) || (IsEitherExclusive && (Rules->Minimum == Rules->Maximum)))
        {
            return TableError(table, descriptor, "the bounds leave no valid value");
        }
    }

    bool IsString = PropertyInternal_IsStringType(descriptor->Type);
    if (IsString && (Rules->MaxLength == 0U))
    {
        return TableError(table, descriptor, "a string needs a MaxLength");
    }
    if (!IsString && ((Rules->MaxLength != 0U) || Rules->IsNullAllowed))
    {
        return TableError(table, descriptor, "only strings take MaxLength and IsNullAllowed");
    }
    if (IsString && Rules->IsNullAllowed && (descriptor->Default.StringValue != NULL))
    {
        return TableError(table, descriptor, "a string that may be null must default to null");
    }
    return Error_CreateSuccess();
}

static Error CheckEnumTable(const PropertyTable* table, const PropertyDescriptor* descriptor)
{
    const PropertyEnumTable* Enum = descriptor->EnumTable;
    if (descriptor->Type != PropertyType_Enum)
    {
        return (Enum == NULL) ? Error_CreateSuccess() : TableError(table, descriptor, "only enums take an enum table");
    }
    if ((Enum == NULL) || (Enum->Entries == NULL) || (Enum->EntryCount == 0U))
    {
        return TableError(table, descriptor, "an enum needs an enum table with entries");
    }

    int64_t StorageMaximum = (descriptor->Size == 1U) ? PROPERTY_ENUM_UINT8_MAX
        : ((descriptor->Size == 2U) ? PROPERTY_ENUM_UINT16_MAX : (int64_t)INT32_MAX);
    int64_t StorageMinimum = (descriptor->Size == 4U) ? (int64_t)INT32_MIN : 0;
    for (size_t Index = 0; Index < Enum->EntryCount; Index++)
    {
        const PropertyEnumEntry* Entry = &Enum->Entries[Index];
        if (!IsSnakeCase(Entry->Name))
        {
            return TableError(table, descriptor, "an enum name is not snake_case");
        }
        if (((int64_t)Entry->Value < StorageMinimum) || ((int64_t)Entry->Value > StorageMaximum))
        {
            return TableError(table, descriptor, "an enum value does not fit the field");
        }
        for (size_t Other = Index + 1U; Other < Enum->EntryCount; Other++)
        {
            if (AreStringsEqual(Enum->Entries[Other].Name, Entry->Name) || (Enum->Entries[Other].Value == Entry->Value))
            {
                return TableError(table, descriptor, "an enum name or value appears twice");
            }
        }
    }
    return Error_CreateSuccess();
}

/* Keys and wire ids unique, fields not overlapping. */
static Error CheckDescriptorPair(const PropertyTable* table, const PropertyDescriptor* first,
    const PropertyDescriptor* second)
{
    if (AreStringsEqual(first->Key, second->Key))
    {
        return TableError(table, second, "the key is used twice");
    }
    if (first->WireId == second->WireId)
    {
        return TableError(table, second, "the wire id is used twice");
    }
    if ((first->Offset < (second->Offset + second->Size)) && (second->Offset < (first->Offset + first->Size)))
    {
        return TableError(table, second, "the field overlaps another property's");
    }
    return Error_CreateSuccess();
}


// Internal functions (PropertyDescriptorInternal.h).
bool PropertyInternal_IsStringType(PropertyType type)
{
    return (type == PropertyType_String) || (type == PropertyType_AssetName);
}

PropertyData PropertyInternal_ReadField(const PropertyDescriptor* descriptor, const void* instance)
{
    PropertyData Data;
    Memory_Zero(&Data, sizeof(Data));
    const unsigned char* Field = (const unsigned char*)instance + descriptor->Offset;
    if (descriptor->Type == PropertyType_Enum)
    {
        Data.EnumValue = ReadEnumStorage(Field, descriptor->Size);
        return Data;
    }
    Memory_Copy(Field, &Data, descriptor->Size);
    return Data;
}

void PropertyInternal_WriteField(const PropertyDescriptor* descriptor, void* instance, const PropertyData* data)
{
    unsigned char* Field = (unsigned char*)instance + descriptor->Offset;
    if (descriptor->Type == PropertyType_Enum)
    {
        WriteEnumStorage(Field, descriptor->Size, data->EnumValue);
        return;
    }
    Memory_Copy(data, Field, descriptor->Size);
}

void PropertyInternal_AdoptString(const PropertyDescriptor* descriptor, void* instance, unsigned char* text)
{
    unsigned char* Previous = ReadOwnedString(descriptor, instance);
    Memory_Copy(&text, (unsigned char*)instance + descriptor->Offset, sizeof(text));
    if ((Previous != NULL) && (Previous != text))
    {
        Memory_Free(Previous);
    }
}

bool PropertyInternal_AreDataEqual(PropertyType type, const PropertyData* a, const PropertyData* b)
{
    switch (type)
    {
        case PropertyType_Bool:
            return a->BoolValue == b->BoolValue;
        case PropertyType_Int32:
            return a->Int32Value == b->Int32Value;
        case PropertyType_UInt32:
            return a->UInt32Value == b->UInt32Value;
        case PropertyType_UInt64:
            return a->UInt64Value == b->UInt64Value;
        case PropertyType_Float:
        case PropertyType_Angle:
            return a->FloatValue == b->FloatValue;
        case PropertyType_Double:
            return a->DoubleValue == b->DoubleValue;
        case PropertyType_Vector2:
            return (a->Vector2Value.x == b->Vector2Value.x) && (a->Vector2Value.y == b->Vector2Value.y);
        case PropertyType_Vector3:
            return (a->Vector3Value.x == b->Vector3Value.x) && (a->Vector3Value.y == b->Vector3Value.y)
                && (a->Vector3Value.z == b->Vector3Value.z);
        case PropertyType_Color:
            return (a->ColorValue.r == b->ColorValue.r) && (a->ColorValue.g == b->ColorValue.g)
                && (a->ColorValue.b == b->ColorValue.b) && (a->ColorValue.a == b->ColorValue.a);
        case PropertyType_RenderColor:
            return (a->RenderColorValue.Tint.r == b->RenderColorValue.Tint.r)
                && (a->RenderColorValue.Tint.g == b->RenderColorValue.Tint.g)
                && (a->RenderColorValue.Tint.b == b->RenderColorValue.Tint.b)
                && (a->RenderColorValue.Brightness == b->RenderColorValue.Brightness)
                && (a->RenderColorValue.Opacity == b->RenderColorValue.Opacity);
        case PropertyType_String:
        case PropertyType_AssetName:
            return AreStringsEqual(a->StringValue, b->StringValue);
        case PropertyType_Enum:
            return a->EnumValue == b->EnumValue;
        case PropertyType_ObjectId:
            return a->ObjectIdValue == b->ObjectIdValue;
        case PropertyType_Date:
            return a->DateValue == b->DateValue;
    }
    return false;
}

bool PropertyInternal_IsDefault(const PropertyDescriptor* descriptor, const void* instance)
{
    PropertyData Data = PropertyInternal_ReadField(descriptor, instance);
    return PropertyInternal_AreDataEqual(descriptor->Type, &Data, &descriptor->Default);
}

const PropertyEnumEntry* PropertyInternal_FindEnumByValue(const PropertyEnumTable* table, int32_t value)
{
    for (size_t Index = 0; Index < table->EntryCount; Index++)
    {
        if (table->Entries[Index].Value == value)
        {
            return &table->Entries[Index];
        }
    }
    return NULL;
}

Error PropertyInternal_AppendEnumNames(const PropertyEnumTable* table, GenericBuffer* destination)
{
    Error Result = Error_CreateSuccess();
    for (size_t Index = 0; (Index < table->EntryCount) && (Result.Code == ErrorCode_Success); Index++)
    {
        const char* Separator = (Index == 0U) ? "" : (((Index + 1U) == table->EntryCount) ? " or " : ", ");
        Result = StringUTF8_Format(u8"%s\"%s\"", destination, Separator, (const char*)table->Entries[Index].Name);
    }
    return Result;
}

const PropertyEnumEntry* PropertyInternal_FindEnumByName(const PropertyEnumTable* table, const unsigned char* name,
    size_t byteCount)
{
    for (size_t Index = 0; Index < table->EntryCount; Index++)
    {
        const unsigned char* Candidate = table->Entries[Index].Name;
        if ((StringUTF8_GetByteLength(Candidate) == byteCount) && ((byteCount == 0U) || Memory_IsEqual(Candidate, name,
            byteCount)))
        {
            return &table->Entries[Index];
        }
    }
    return NULL;
}

Error PropertyInternal_CheckData(const PropertyTable* table, const PropertyDescriptor* descriptor, const PropertyData* data)
{
    switch (descriptor->Type)
    {
        case PropertyType_Bool:
        case PropertyType_UInt64:
        case PropertyType_Color:
        case PropertyType_ObjectId:
            return Error_CreateSuccess();
        case PropertyType_Int32:
            return CheckInteger(table, descriptor, (int64_t)data->Int32Value);
        case PropertyType_UInt32:
            return CheckInteger(table, descriptor, (int64_t)data->UInt32Value);
        case PropertyType_Float:
        case PropertyType_Angle:
            return CheckFloats(table, descriptor, &data->FloatValue, 1U);
        case PropertyType_Double:
            return CheckDouble(table, descriptor, data->DoubleValue);
        case PropertyType_Date:
            return GameCalendar_IsDayNumberValid(data->DateValue) ? Error_CreateSuccess()
                : MakeRuleError(table, descriptor, "be a date in the years 1 to 9999");
        case PropertyType_Vector2:
        {
            const float Components[2] = { data->Vector2Value.x, data->Vector2Value.y };
            return CheckFloats(table, descriptor, Components, 2U);
        }
        case PropertyType_Vector3:
        {
            const float Components[3] = { data->Vector3Value.x, data->Vector3Value.y, data->Vector3Value.z };
            return CheckFloats(table, descriptor, Components, 3U);
        }
        case PropertyType_RenderColor:
        {
            float Brightness = data->RenderColorValue.Brightness;
            float Opacity = data->RenderColorValue.Opacity;
            bool IsValid = IsFloatFinite(Brightness) && IsFloatFinite(Opacity)
                && (Brightness >= RENDER_COLOR_BRIGHTNESS_MIN) && (Brightness <= RENDER_COLOR_BRIGHTNESS_MAX)
                && (Opacity >= RENDER_COLOR_OPACITY_MIN) && (Opacity <= RENDER_COLOR_OPACITY_MAX);
            return IsValid ? Error_CreateSuccess()
                : MakeRuleError(table, descriptor, "have a brightness and an opacity in [0, 1]");
        }
        case PropertyType_String:
        case PropertyType_AssetName:
            return CheckString(table, descriptor, data->StringValue);
        case PropertyType_Enum:
            return CheckEnum(table, descriptor, data->EnumValue);
    }
    return MakeRuleError(table, descriptor, "have a known property type");
}

Error PropertyInternal_WrapError(ErrorCode code, const PropertyTable* table, const PropertyDescriptor* descriptor,
    Error* inner)
{
    const char* Problem = (inner->Message != NULL) ? (const char*)inner->Message : "invalid value.";
    Error Result = (descriptor != NULL)
        ? Error_Construct4(code, "%s.%s: %s", (const char*)table->Name, (const char*)descriptor->Key, Problem)
        : Error_Construct4(code, "%s: %s", (const char*)table->Name, Problem);
    Error_Deconstruct(inner);
    return Result;
}

Error PropertyInternal_Recode(Error* inner, ErrorCode code)
{
    if ((inner->Code == ErrorCode_Success) || (inner->Code == code))
    {
        Error Same = *inner;
        *inner = Error_CreateSuccess();
        return Same;
    }
    Error Result = Error_Construct4(code, "%s", (inner->Message != NULL) ? (const char*)inner->Message : "invalid value.");
    Error_Deconstruct(inner);
    return Result;
}

void PropertyScratch_Begin(PropertyScratch* self, const PropertyTable* table, const void* instance)
{
    self->_isHeap = table->StructSize > PROPERTY_SCRATCH_INLINE_SIZE;
    self->_instance = self->_isHeap ? Memory_Allocate(table->StructSize) : (void*)self->_inline.Bytes;
    Memory_Zero(self->_instance, table->StructSize);

    for (size_t Index = 0; Index < table->DescriptorCount; Index++)
    {
        const PropertyDescriptor* Descriptor = &table->Descriptors[Index];
        PropertyData Data = PropertyInternal_HasFlags(Descriptor, PropertyFlags_Persistent) ? Descriptor->Default
            : PropertyInternal_ReadField(Descriptor, instance);
        if (PropertyInternal_IsStringType(Descriptor->Type))
        {
            PropertyInternal_AdoptString(Descriptor, self->_instance, CloneString(Data.StringValue));
            continue;
        }
        PropertyInternal_WriteField(Descriptor, self->_instance, &Data);
    }
}

/* Releases the scratch's storage; its strings must have been released or moved out. */
static void EndScratch(PropertyScratch* self)
{
    if (self->_isHeap)
    {
        Memory_Free(self->_instance);
    }
    self->_instance = NULL;
    self->_isHeap = false;
}

Error PropertyScratch_Commit(PropertyScratch* self, const PropertyTable* table, void* instance, ErrorCode failureCode)
{
    Error ValidateResult = Properties_Validate(table, self->_instance);
    if (ValidateResult.Code != ErrorCode_Success)
    {
        PropertyScratch_Abort(self, table);
        return PropertyInternal_Recode(&ValidateResult, failureCode);
    }

    for (size_t Index = 0; Index < table->DescriptorCount; Index++)
    {
        const PropertyDescriptor* Descriptor = &table->Descriptors[Index];
        if (PropertyInternal_IsStringType(Descriptor->Type))
        {
            PropertyInternal_AdoptString(Descriptor, instance, ReadOwnedString(Descriptor, self->_instance));
            continue;
        }
        PropertyData Data = PropertyInternal_ReadField(Descriptor, self->_instance);
        PropertyInternal_WriteField(Descriptor, instance, &Data);
    }
    EndScratch(self);

    if (table->AfterSet != NULL)
    {
        table->AfterSet(instance, NULL);
    }
    return Error_CreateSuccess();
}

void PropertyScratch_Abort(PropertyScratch* self, const PropertyTable* table)
{
    if (self->_instance == NULL)
    {
        return;
    }
    Properties_Deconstruct(table, self->_instance);
    EndScratch(self);
}


// Public functions.
const unsigned char* PropertyType_GetName(PropertyType type)
{
    switch (type)
    {
        case PropertyType_Bool:
            return u8"bool";
        case PropertyType_Int32:
            return u8"int32";
        case PropertyType_UInt32:
            return u8"uint32";
        case PropertyType_UInt64:
            return u8"uint64";
        case PropertyType_Float:
            return u8"float";
        case PropertyType_Double:
            return u8"double";
        case PropertyType_Angle:
            return u8"angle";
        case PropertyType_Vector2:
            return u8"vector2";
        case PropertyType_Vector3:
            return u8"vector3";
        case PropertyType_Color:
            return u8"color";
        case PropertyType_RenderColor:
            return u8"render_color";
        case PropertyType_String:
            return u8"string";
        case PropertyType_AssetName:
            return u8"asset_name";
        case PropertyType_Enum:
            return u8"enum";
        case PropertyType_ObjectId:
            return u8"object_id";
        case PropertyType_Date:
            return u8"date";
    }
    return u8"unknown";
}

Error PropertyTable_Check(const PropertyTable* table)
{
    if (table == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "PropertyTable_Check: table must not be NULL.");
    }
    Error Result = CheckTableHeader(table);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    for (size_t Index = 0; Index < table->DescriptorCount; Index++)
    {
        const PropertyDescriptor* Descriptor = &table->Descriptors[Index];
        Result = CheckDescriptorShape(table, Descriptor);
        if (Result.Code == ErrorCode_Success)
        {
            Result = CheckDescriptorRules(table, Descriptor);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = CheckEnumTable(table, Descriptor);
        }
        for (size_t Other = 0; (Other < Index) && (Result.Code == ErrorCode_Success); Other++)
        {
            Result = CheckDescriptorPair(table, &table->Descriptors[Other], Descriptor);
        }
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        Error DefaultResult = PropertyInternal_CheckData(table, Descriptor, &Descriptor->Default);
        if (DefaultResult.Code != ErrorCode_Success)
        {
            Error Wrapped = Error_Construct4(ErrorCode_IllegalArgument, "Property table %s: the default breaks its rules: %s",
                (const char*)table->Name, (DefaultResult.Message != NULL) ? (const char*)DefaultResult.Message : "?");
            Error_Deconstruct(&DefaultResult);
            return Wrapped;
        }
    }
    return Error_CreateSuccess();
}

const PropertyDescriptor* PropertyTable_FindByKey(const PropertyTable* table, const unsigned char* key)
{
    if ((table == NULL) || (key == NULL))
    {
        return NULL;
    }
    for (size_t Index = 0; Index < table->DescriptorCount; Index++)
    {
        if (AreStringsEqual(table->Descriptors[Index].Key, key))
        {
            return &table->Descriptors[Index];
        }
    }
    return NULL;
}

Error Properties_Construct(const PropertyTable* table, void* instance)
{
    if ((table == NULL) || (instance == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Properties_Construct: table and instance must not be NULL.");
    }

    Memory_Zero(instance, table->StructSize);
    for (size_t Index = 0; Index < table->DescriptorCount; Index++)
    {
        const PropertyDescriptor* Descriptor = &table->Descriptors[Index];
        if (PropertyInternal_IsStringType(Descriptor->Type))
        {
            PropertyInternal_AdoptString(Descriptor, instance, CloneString(Descriptor->Default.StringValue));
            continue;
        }
        PropertyInternal_WriteField(Descriptor, instance, &Descriptor->Default);
    }
    return Error_CreateSuccess();
}

void Properties_Deconstruct(const PropertyTable* table, void* instance)
{
    if ((table == NULL) || (instance == NULL))
    {
        return;
    }
    for (size_t Index = 0; Index < table->DescriptorCount; Index++)
    {
        const PropertyDescriptor* Descriptor = &table->Descriptors[Index];
        if (PropertyInternal_IsStringType(Descriptor->Type))
        {
            PropertyInternal_AdoptString(Descriptor, instance, NULL);
        }
    }
}

Error Properties_Validate(const PropertyTable* table, const void* instance)
{
    if ((table == NULL) || (instance == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Properties_Validate: table and instance must not be NULL.");
    }
    for (size_t Index = 0; Index < table->DescriptorCount; Index++)
    {
        const PropertyDescriptor* Descriptor = &table->Descriptors[Index];
        PropertyData Data = PropertyInternal_ReadField(Descriptor, instance);
        Error Result = PropertyInternal_CheckData(table, Descriptor, &Data);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}

Error Properties_CheckValue(const PropertyTable* table, const PropertyDescriptor* descriptor, const PropertyValue* value)
{
    if ((table == NULL) || (descriptor == NULL) || (value == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "Properties_CheckValue: table, descriptor and value must not be NULL.");
    }
    if (value->Type != descriptor->Type)
    {
        return Error_Construct4(ErrorCode_IllegalArgument, "%s.%s expects a %s value, not a %s.", (const char*)table->Name,
            (const char*)descriptor->Key, (const char*)PropertyType_GetName(descriptor->Type),
            (const char*)PropertyType_GetName(value->Type));
    }
    return PropertyInternal_CheckData(table, descriptor, &value->Data);
}

Error Properties_CopyTo(const PropertyTable* table, const void* source, void* destination)
{
    if ((table == NULL) || (source == NULL) || (destination == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "Properties_CopyTo: table, source and destination must not be NULL.");
    }
    if (source == destination)
    {
        return Error_CreateSuccess();
    }

    for (size_t Index = 0; Index < table->DescriptorCount; Index++)
    {
        const PropertyDescriptor* Descriptor = &table->Descriptors[Index];
        if (PropertyInternal_IsStringType(Descriptor->Type))
        {
            PropertyInternal_AdoptString(Descriptor, destination, CloneString(ReadOwnedString(Descriptor, source)));
            continue;
        }
        PropertyData Data = PropertyInternal_ReadField(Descriptor, source);
        PropertyInternal_WriteField(Descriptor, destination, &Data);
    }

    if (table->AfterSet != NULL)
    {
        table->AfterSet(destination, NULL);
    }
    return Error_CreateSuccess();
}

bool Properties_Equals(const PropertyTable* table, const void* a, const void* b)
{
    if ((table == NULL) || (a == NULL) || (b == NULL))
    {
        return false;
    }
    for (size_t Index = 0; Index < table->DescriptorCount; Index++)
    {
        const PropertyDescriptor* Descriptor = &table->Descriptors[Index];
        PropertyData First = PropertyInternal_ReadField(Descriptor, a);
        PropertyData Second = PropertyInternal_ReadField(Descriptor, b);
        if (!PropertyInternal_AreDataEqual(Descriptor->Type, &First, &Second))
        {
            return false;
        }
    }
    return true;
}

Error Properties_Get(const PropertyTable* table, const void* instance, const unsigned char* key, PropertyValue* outValue)
{
    if ((table == NULL) || (instance == NULL) || (key == NULL) || (outValue == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "Properties_Get: table, instance, key and outValue must not be NULL.");
    }
    const PropertyDescriptor* Descriptor = PropertyTable_FindByKey(table, key);
    if (Descriptor == NULL)
    {
        return Error_Construct4(ErrorCode_IllegalArgument, "%s has no property \"%s\".", (const char*)table->Name,
            (const char*)key);
    }
    outValue->Type = Descriptor->Type;
    outValue->Data = PropertyInternal_ReadField(Descriptor, instance);
    return Error_CreateSuccess();
}

Error Properties_Set(const PropertyTable* table, void* instance, const unsigned char* key, const PropertyValue* value)
{
    if ((table == NULL) || (instance == NULL) || (key == NULL) || (value == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "Properties_Set: table, instance, key and value must not be NULL.");
    }
    const PropertyDescriptor* Descriptor = PropertyTable_FindByKey(table, key);
    if (Descriptor == NULL)
    {
        return Error_Construct4(ErrorCode_IllegalArgument, "%s has no property \"%s\".", (const char*)table->Name,
            (const char*)key);
    }
    if (PropertyInternal_HasFlags(Descriptor, PropertyFlags_ReadOnly))
    {
        return Error_Construct4(ErrorCode_InvalidOperation, "%s.%s is read-only.", (const char*)table->Name,
            (const char*)Descriptor->Key);
    }
    Error CheckResult = Properties_CheckValue(table, Descriptor, value);
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }

    if (PropertyInternal_IsStringType(Descriptor->Type))
    {
        PropertyInternal_AdoptString(Descriptor, instance, CloneString(value->Data.StringValue));
    }
    else
    {
        PropertyInternal_WriteField(Descriptor, instance, &value->Data);
    }
    if (table->AfterSet != NULL)
    {
        table->AfterSet(instance, Descriptor);
    }
    return Error_CreateSuccess();
}
