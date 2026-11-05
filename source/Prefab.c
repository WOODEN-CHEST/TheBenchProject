// The prefab asset's data: reading a file (Prefab.h), checking it as hostile, and what a copy reads of it. The asset type itself is
// PrefabDefinition.c, what makes copies WorldPrefabs.c.

#include <inttypes.h>
#include <stdio.h>
#include "Prefab.h"
#include "PrefabInternal.h"
#include "AssetDefinitionCommon.h"
#include "GameJSON.h"
#include "WorldComponent.h"
#include "WorldFile.h"
#include "WorldInternal.h"
#include "wr/WRCollection.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"


// Macros.
#define OBJECTS_KEY ((const unsigned char*)u8"objects")
#define FORMAT_VERSION_KEY ((const unsigned char*)u8"format_version")
#define PROPERTIES_KEY ((const unsigned char*)u8"properties")
#define TARGETS_KEY ((const unsigned char*)u8"targets")
#define TYPE_KEY ((const unsigned char*)u8"type")
#define OPTIONS_KEY ((const unsigned char*)u8"options")
#define OPTION_DISPLAY_NAMES_KEY ((const unsigned char*)u8"option_display_names")
#define MINIMUM_KEY ((const unsigned char*)u8"minimum")
#define MAXIMUM_KEY ((const unsigned char*)u8"maximum")
#define DEFAULT_KEY ((const unsigned char*)u8"default")
#define NAME_ROW_KEY ((const unsigned char*)u8"name")
/** How many bytes of a name from the file a message shows. */
#define PRINT_CAPACITY ((size_t)129)
/** A number of degrees in radians. */
#define PREFAB_DEGREES_TO_RADIANS (3.14159265358979323846 / 180.0)
/** The inspector's categories. */
#define CATEGORY_TARGET ((const unsigned char*)u8"Target")
#define CATEGORY_PROPERTY ((const unsigned char*)u8"Property")
/** The longest row key, component name or tag a target's record names. */
#define TARGET_NAME_MAX_LENGTH ((size_t)128)


// Types.
/* An object's place in the prefab. */
typedef struct PrefabObjectStruct
{
    const unsigned char* Name;
    const WorldObject* Template;
    uint32_t Parent;
} PrefabObject;

/* An object's name in the sorted index. */
typedef struct PrefabNameEntryStruct
{
    const unsigned char* Name;
    size_t ByteCount;
    uint32_t Index;
} PrefabNameEntry;

/* A property's plain rows, as a file writes them: a record of the "prefab_property" table. */
typedef struct PrefabPropertyRecordStruct
{
    PrefabPropertyType Type;
    unsigned char* DisplayName;
    unsigned char* Category;
    unsigned char* Description;
    unsigned char* AssetType;
    DataValue* Default;
    double Minimum;
    double Maximum;
} PrefabPropertyRecord;

struct PrefabStruct
{
    /* How many bytes its file has. */
    size_t _fileByteCount;
    /* The registry of the template's tags and data: an asset may serve worlds of other registries. */
    KeyRegistry _keys;
    bool _hasKeys;
    /* The objects, in a world that is never shown. */
    World _template;
    bool _hasTemplate;
    PrefabObject* _objects;
    size_t _objectCount;
    PrefabNameEntry* _nameOrder;
    PrefabProperty* _properties;
    PrefabPropertyRecord* _records;
    size_t _propertyCount;
    /* What the properties' resolved parts are made of, each freed once when it goes. */
    GenericBuffer _blocks;
    GenericBuffer _lists;
    GenericBuffer _texts;
    /* How many objects the targets reach so far, and how many the tags among them looked through. */
    size_t _targetObjectCount;
    size_t _tagScanCount;
};

/* What reading the properties needs. */
typedef struct PrefabReadStruct
{
    Prefab* Prefab;
    JSONObjectPool* Pool;
    AssetManager* Manager;
    /* The property being read. */
    size_t Index;
    JSONCompound* OptionDisplayNames;
    /* An enum's options while they are read. */
    PrefabOption* Options;
    size_t OptionCount;
} PrefabRead;

typedef Error (*PrefabEntryHandler)(PrefabRead* read, const JSONCompoundEntry* entry);


// Fields.
static const PropertyEnumEntry PropertyTypeEntries[] =
{
    { .Name = u8"bool", .Value = PrefabPropertyType_Bool },
    { .Name = u8"int", .Value = PrefabPropertyType_Int },
    { .Name = u8"float", .Value = PrefabPropertyType_Float },
    { .Name = u8"color", .Value = PrefabPropertyType_Color },
    { .Name = u8"vector", .Value = PrefabPropertyType_Vector },
    { .Name = u8"vector2", .Value = PrefabPropertyType_Vector2 },
    { .Name = u8"angle", .Value = PrefabPropertyType_Angle },
    { .Name = u8"enum", .Value = PrefabPropertyType_Enum },
    { .Name = u8"asset", .Value = PrefabPropertyType_Asset },
    { .Name = u8"string", .Value = PrefabPropertyType_String },
};

static const PropertyEnumTable PropertyTypeEnum =
{
    .Name = u8"prefab_property_type", .Entries = PropertyTypeEntries,
    .EntryCount = sizeof(PropertyTypeEntries) / sizeof(PropertyTypeEntries[0])
};

static const PropertyEnumEntry AxisEntries[] =
{
    { .Name = u8"none", .Value = PrefabAxis_None },
    { .Name = u8"x", .Value = PrefabAxis_X },
    { .Name = u8"y", .Value = PrefabAxis_Y },
    { .Name = u8"z", .Value = PrefabAxis_Z },
    { .Name = u8"all", .Value = PrefabAxis_All },
};

static const PropertyEnumTable AxisEnum =
{
    .Name = u8"prefab_axis", .Entries = AxisEntries, .EntryCount = sizeof(AxisEntries) / sizeof(AxisEntries[0])
};

/* The wire ids exist for the table's rules: prefab files are JSON. */
static const PropertyDescriptor TargetProperties[] =
{
    {
        .Key = u8"object", .WireId = 1U, .Type = PropertyType_LocalName, PROPERTY_MEMBER(PrefabTargetRecord, Object),
        .Rules = PROPERTY_RULES_STRING(TARGET_NAME_MAX_LENGTH, true), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_TARGET, .DisplayName = u8"Object", .Description = u8"The object it reaches, by its name in the prefab.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"tag", .WireId = 2U, .Type = PropertyType_String, PROPERTY_MEMBER(PrefabTargetRecord, Tag),
        .Rules = PROPERTY_RULES_STRING(TARGET_NAME_MAX_LENGTH, true), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_TARGET, .DisplayName = u8"Tag", .Description = u8"Or every object of the prefab with this tag.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"component", .WireId = 3U, .Type = PropertyType_LocalName, PROPERTY_MEMBER(PrefabTargetRecord, Component),
        .Rules = PROPERTY_RULES_STRING(TARGET_NAME_MAX_LENGTH, true), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_TARGET, .DisplayName = u8"Component",
        .Description = u8"The component whose row it writes, by its name in world files. None: the object's own rows.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"row", .WireId = 4U, .Type = PropertyType_LocalName, PROPERTY_MEMBER(PrefabTargetRecord, Row),
        .Rules = PROPERTY_RULES_STRING(TARGET_NAME_MAX_LENGTH, true), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_TARGET, .DisplayName = u8"Row", .Description = u8"The row it writes. An override's flag is a row too.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"property", .WireId = 5U, .Type = PropertyType_LocalName, PROPERTY_MEMBER(PrefabTargetRecord, Property),
        .Rules = PROPERTY_RULES_STRING(TARGET_NAME_MAX_LENGTH, true), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_TARGET, .DisplayName = u8"Nested property",
        .Description = u8"Or the property it passes to, of the prefab its object holds.", .SinceVersion = 1U
    },
    {
        .Key = u8"axis", .WireId = 6U, .Type = PropertyType_Enum, PROPERTY_MEMBER(PrefabTargetRecord, Axis),
        .Default.EnumValue = PrefabAxis_None, .EnumTable = &AxisEnum, .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_TARGET, .DisplayName = u8"Axis",
        .Description = u8"A number written into this axis of a vector row, the others left as they are; all writes every axis.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"value", .WireId = 7U, .Type = PropertyType_DataValue, PROPERTY_MEMBER(PrefabTargetRecord, Value),
        .Rules = PROPERTY_RULES_DATA_VALUE(PREFAB_VALUE_MAX_BYTES, false), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_TARGET, .DisplayName = u8"Value",
        .Description = u8"An enum option's value to set, as the row's JSON form.", .SinceVersion = 1U
    },
};

static const PropertyTable TargetTable =
{
    .Name = u8"prefab_target", .Version = 1U, .StructSize = sizeof(PrefabTargetRecord), .Descriptors = TargetProperties,
    .DescriptorCount = sizeof(TargetProperties) / sizeof(TargetProperties[0])
};

static const PropertyDescriptor PropertyProperties[] =
{
    {
        .Key = u8"type", .WireId = 1U, .Type = PropertyType_Enum, PROPERTY_MEMBER(PrefabPropertyRecord, Type),
        .Default.EnumValue = PrefabPropertyType_Float, .EnumTable = &PropertyTypeEnum, .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_PROPERTY, .DisplayName = u8"Type", .Description = u8"What kind of value it holds.", .SinceVersion = 1U
    },
    {
        .Key = u8"display_name", .WireId = 2U, .Type = PropertyType_String, PROPERTY_MEMBER(PrefabPropertyRecord, DisplayName),
        .Rules = PROPERTY_RULES_STRING(PREFAB_TEXT_MAX_LENGTH, true), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_PROPERTY, .DisplayName = u8"Display name", .Description = u8"What the options panel calls it.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"category", .WireId = 3U, .Type = PropertyType_String, PROPERTY_MEMBER(PrefabPropertyRecord, Category),
        .Rules = PROPERTY_RULES_STRING(PREFAB_TEXT_MAX_LENGTH, true), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_PROPERTY, .DisplayName = u8"Category", .Description = u8"The options panel's group it goes in.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"description", .WireId = 4U, .Type = PropertyType_String, PROPERTY_MEMBER(PrefabPropertyRecord, Description),
        .Rules = PROPERTY_RULES_STRING(PREFAB_TEXT_MAX_LENGTH, true), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_PROPERTY, .DisplayName = u8"Description", .Description = u8"The options panel's tooltip.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"asset_type", .WireId = 5U, .Type = PropertyType_LocalName, PROPERTY_MEMBER(PrefabPropertyRecord, AssetType),
        .Rules = PROPERTY_RULES_STRING(TARGET_NAME_MAX_LENGTH, true), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_PROPERTY, .DisplayName = u8"Asset type",
        .Description = u8"For an asset property: the asset type the panel offers. None: any.", .SinceVersion = 1U
    },
    {
        .Key = u8"default", .WireId = 6U, .Type = PropertyType_DataValue, PROPERTY_MEMBER(PrefabPropertyRecord, Default),
        .Rules = PROPERTY_RULES_DATA_VALUE(PREFAB_VALUE_MAX_BYTES, false), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_PROPERTY, .DisplayName = u8"Default", .Description = u8"Its value where a copy sets none.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"minimum", .WireId = 7U, .Type = PropertyType_Double, PROPERTY_MEMBER(PrefabPropertyRecord, Minimum),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_PROPERTY, .DisplayName = u8"Minimum",
        .Description = u8"The smallest number it may be. Left out: no limit.", .SinceVersion = 1U
    },
    {
        .Key = u8"maximum", .WireId = 8U, .Type = PropertyType_Double, PROPERTY_MEMBER(PrefabPropertyRecord, Maximum),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_PROPERTY, .DisplayName = u8"Maximum",
        .Description = u8"The largest number it may be. Left out: no limit.", .SinceVersion = 1U
    },
};

static const PropertyTable PropertyTable_ =
{
    .Name = u8"prefab_property", .Version = 1U, .StructSize = sizeof(PrefabPropertyRecord), .Descriptors = PropertyProperties,
    .DescriptorCount = sizeof(PropertyProperties) / sizeof(PropertyProperties[0])
};

static const unsigned char* const RootKeys[] = { u8"name", u8"format_version", u8"properties", u8"objects" };
static const unsigned char* const BlockKeys[] =
{
    u8"type", u8"display_name", u8"category", u8"description", u8"asset_type", u8"default", u8"minimum", u8"maximum", u8"targets",
    u8"options", u8"option_display_names"
};


// Static functions: small things.
/* A block a prefab owns until it goes. */
static void* AllocateBlock(Prefab* self, size_t byteCount)
{
    void* Block = Memory_Allocate((byteCount > 0U) ? byteCount : 1U);
    Memory_Zero(Block, (byteCount > 0U) ? byteCount : 1U);
    GenericBuffer_AddLast(&self->_blocks, &Block);
    return Block;
}

/* A copy of a name a prefab owns until it goes. */
static const unsigned char* KeepText(Prefab* self, const unsigned char* text)
{
    size_t Length = StringUTF8_GetByteLength(text);
    unsigned char* Copy = Memory_Allocate(Length + 1U);
    Memory_Copy(text, Copy, Length + 1U);
    GenericBuffer_AddLast(&self->_texts, &Copy);
    return Copy;
}

/* "<where>: <inner>", as InvalidJSON, printable; deconstructs @p inner. */
static Error WrapWhere(const char* where, Error* inner)
{
    Error Wrapped = Error_Construct4(ErrorCode_InvalidJSON, "%s: %s", where,
        (inner->Message != NULL) ? (const char*)inner->Message : "invalid.");
    Error_Deconstruct(inner);
    return Wrapped;
}

/* Calls @p handler for every entry of a compound, in the file's order. */
static Error ForEachEntry(PrefabRead* read, JSONCompound* compound, PrefabEntryHandler handler)
{
    CollectionEnumerator* Enumerator = ICollection_CreateEnumerator(JSONCompound_GetEntryCollection(compound));
    if (Enumerator == NULL)
    {
        return Error_Construct2(ErrorCode_BufferTooSmall, "Could not enumerate a compound.");
    }
    bool HasNext = false;
    Error Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
    while ((Result.Code == ErrorCode_Success) && HasNext)
    {
        JSONCompoundEntry Entry;
        Result = CollectionEnumerator_NextByValue(Enumerator, &Entry);
        if (Result.Code == ErrorCode_Success)
        {
            Result = handler(read, &Entry);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
        }
    }
    CollectionEnumerator_Destroy(Enumerator);
    return Result;
}

static bool IsNumber(const DataValue* value)
{
    return (value->Type == DataType_Int) || (value->Type == DataType_Float);
}

static double GetNumber(const DataValue* value)
{
    return (value->Type == DataType_Int) ? (double)value->IntValue : value->FloatValue;
}

static bool IsVectorRow(const PropertyDescriptor* row)
{
    return (row->Type == PropertyType_Vector2) || (row->Type == PropertyType_Vector3) || (row->Type == PropertyType_AngleVector3)
        || (row->Type == PropertyType_Vector3Double);
}

/* Whether a row of a type takes a number per axis, and this axis. */
static bool DoesRowTakeAxis(const PropertyDescriptor* row, PrefabAxis axis)
{
    return IsVectorRow(row) && ((row->Type != PropertyType_Vector2) || (axis != PrefabAxis_Z));
}

/* Checks a number against a property's range. */
static Error CheckRange(const PrefabProperty* property, double number)
{
    if ((property->HasMinimum && (number < property->Minimum)) || (property->HasMaximum && (number > property->Maximum)))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange, "the number %g lies outside its range.", number);
    }
    return Error_CreateSuccess();
}

/* A vector's value: a list of @p count numbers, or a vector3 for three, each in range. */
static Error CheckVector(const PrefabProperty* property, const DataValue* value, size_t count)
{
    if ((value->Type == DataType_Vector3) && (count == 3U))
    {
        Error Result = CheckRange(property, value->Vector3Value.X);
        if (Result.Code == ErrorCode_Success)
        {
            Result = CheckRange(property, value->Vector3Value.Y);
        }
        return (Result.Code == ErrorCode_Success) ? CheckRange(property, value->Vector3Value.Z) : Result;
    }
    if ((value->Type != DataType_List) || (DataValue_GetListCount(value) != count))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange, "the value must be a list of %" PRIu64 " numbers.", (uint64_t)count);
    }
    for (size_t Index = 0U; Index < count; Index++)
    {
        const DataValue* Element = DataValue_GetListElement(value, Index);
        if (!IsNumber(Element))
        {
            return Error_Construct4(ErrorCode_ArgumentOutOfRange, "the value must be a list of %" PRIu64 " numbers.",
                (uint64_t)count);
        }
        Error Result = CheckRange(property, GetNumber(Element));
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}

/* A colour's value: whatever a colour row reads, through the same reader. */
static Error CheckColor(const DataValue* value, JSONObjectPool* pool)
{
    JSONObjectValue Json = JSONObjectValue_CreateNull();
    Error Result = DataValue_WritePlainJSON(value, pool, &Json);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Color Read;
    Result = GameJSON_ParseColor(&Json, &Read);
    Error ReturnResult = JSONObjectPool_ReturnValue(pool, &Json);
    Error_Deconstruct(&ReturnResult);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "the value must be a colour.");
    }
    return Result;
}


// Static functions: targets.
/* The objects a target's record reaches, in the prefab's order; those that hold what it writes, for a tag. */
static Error CollectTargetObjects(Prefab* self, const PrefabTargetRecord* record, int32_t componentType, PrefabTarget* outTarget)
{
    if ((record->Object != NULL) == (record->Tag != NULL))
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "a target names exactly one of \"object\" and \"tag\".");
    }
    uint32_t* Reached = NULL;
    size_t Count = 0U;
    if (record->Object != NULL)
    {
        uint32_t Index = 0U;
        if (!Prefab_TryFindObject(self, record->Object, StringUTF8_GetByteLength(record->Object), &Index))
        {
            char Name[PRINT_CAPACITY];
            AssetJSON_CopyPrintable(record->Object, Name, sizeof(Name));
            return Error_Construct4(ErrorCode_InvalidJSON, "no object is named \"%s\".", Name);
        }
        if ((componentType != PREFAB_NO_COMPONENT)
            && (WorldObject_GetComponent(self->_objects[Index].Template, (WorldComponentType)componentType) == NULL))
        {
            char Name[PRINT_CAPACITY];
            AssetJSON_CopyPrintable(record->Object, Name, sizeof(Name));
            return Error_Construct4(ErrorCode_InvalidJSON, "the object \"%s\" has no such component.", Name);
        }
        Reached = AllocateBlock(self, sizeof(uint32_t));
        Reached[0] = Index;
        Count = 1U;
    }
    else
    {
        uint32_t Key = NAMESPACED_KEY_ID_NONE;
        Error Result = KeyRegistry_InternText(&self->_keys, record->Tag, StringUTF8_GetByteLength(record->Tag), NULL, &Key);
        if (Result.Code != ErrorCode_Success)
        {
            return WrapWhere("the tag", &Result);
        }
        // A small file can name a tag many times, and each looks through every object.
        self->_tagScanCount += self->_objectCount;
        if (self->_tagScanCount > PREFAB_MAX_TAG_SCAN)
        {
            return Error_Construct4(ErrorCode_InvalidJSON, "its tag targets look through more than the %" PRIu64 " objects a prefab's may.",
                (uint64_t)PREFAB_MAX_TAG_SCAN);
        }
        for (size_t Pass = 0U; Pass < 2U; Pass++)
        {
            Count = 0U;
            for (size_t Index = 0U; Index < self->_objectCount; Index++)
            {
                const WorldObject* Template = self->_objects[Index].Template;
                if (!WorldObject_HasTag(Template, Key) || ((componentType != PREFAB_NO_COMPONENT)
                    && (WorldObject_GetComponent(Template, (WorldComponentType)componentType) == NULL)))
                {
                    continue;
                }
                if (Pass == 1U)
                {
                    Reached[Count] = (uint32_t)Index;
                }
                Count++;
            }
            if (Count == 0U)
            {
                return Error_Construct2(ErrorCode_InvalidJSON, "no object has this tag and holds what the target writes.");
            }
            if (Pass == 0U)
            {
                Reached = AllocateBlock(self, Count * sizeof(uint32_t));
            }
        }
    }
    self->_targetObjectCount += Count;
    if (self->_targetObjectCount > PREFAB_MAX_TARGET_OBJECTS)
    {
        return Error_Construct4(ErrorCode_InvalidJSON, "its targets reach more than the %" PRIu64 " objects a prefab's may.",
            (uint64_t)PREFAB_MAX_TARGET_OBJECTS);
    }
    outTarget->Objects = Reached;
    outTarget->ObjectCount = Count;
    return Error_CreateSuccess();
}

/* What a target writes: the row's table and the row. */
static Error FindTargetRow(Prefab* self, const PrefabTargetRecord* record, PrefabTarget* outTarget, int32_t* outComponentType)
{
    *outComponentType = PREFAB_NO_COMPONENT;
    const PropertyTable* Table = WorldObject_GetPropertyTable();
    if (record->Component != NULL)
    {
        WorldComponentType Type = WorldComponentType_Model;
        if (!World_TryFindComponentType(&self->_template, record->Component, &Type))
        {
            char Name[PRINT_CAPACITY];
            AssetJSON_CopyPrintable(record->Component, Name, sizeof(Name));
            return Error_Construct4(ErrorCode_InvalidJSON, "no component type is named \"%s\".", Name);
        }
        Table = World_GetComponentTypeInfo(&self->_template, Type)->Table;
        *outComponentType = (int32_t)Type;
    }
    const PropertyDescriptor* Row = PropertyTable_FindByKey(Table, record->Row);
    if (Row == NULL)
    {
        char Name[PRINT_CAPACITY];
        AssetJSON_CopyPrintable(record->Row, Name, sizeof(Name));
        return Error_Construct4(ErrorCode_InvalidJSON, "%s has no row \"%s\".", (const char*)Table->Name, Name);
    }
    if (!PrefabInternal_IsRowWritable(Table, Row))
    {
        return Error_Construct4(ErrorCode_InvalidJSON, "%s.%s cannot be written by a prefab.", (const char*)Table->Name,
            (const char*)Row->Key);
    }
    outTarget->Table = Table;
    outTarget->Row = Row;
    return Error_CreateSuccess();
}

/* Reads a value through a row the way a copy will: on a zeroed block, over the template's value, so that an axis finds the rest of its row as
 * a rule over the whole row wants it. A row's rules are the same for every object it reaches: the first answers for all. */
static Error CheckRowValue(const Prefab* self, const PrefabTarget* target, const DataValue* value, JSONObjectPool* pool)
{
    void* Scratch = Memory_Allocate(target->Table->StructSize);
    Memory_Zero(Scratch, target->Table->StructSize);
    const WorldObject* Object = self->_objects[target->Objects[0]].Template;
    const void* Template = (target->ComponentType == PREFAB_NO_COMPONENT) ? (const void*)Object
        : (const void*)WorldObject_GetComponent(Object, (WorldComponentType)target->ComponentType);
    PropertyValue Base;
    Error Result = Properties_Get(target->Table, Template, target->Row->Key, &Base);
    if (Result.Code == ErrorCode_Success)
    {
        Result = Properties_Set(target->Table, Scratch, target->Row->Key, &Base);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = PrefabInternal_ApplyRowValue(target->Table, Scratch, target->Row, target->Record->Axis, value, pool);
    }
    Properties_Deconstruct(target->Table, Scratch);
    Memory_Free(Scratch);
    return Result;
}

/* A target's record against the prefab's objects. @p value is what it will write: the property's default, or the record's own. */
static Error ResolveTarget(Prefab* self, const PrefabTargetRecord* record, const DataValue* value, JSONObjectPool* pool,
    PrefabTarget* outTarget)
{
    Memory_Zero(outTarget, sizeof(*outTarget));
    outTarget->Record = record;
    outTarget->ComponentType = PREFAB_NO_COMPONENT;
    if ((record->Row != NULL) == (record->Property != NULL))
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "a target names exactly one of \"row\" and \"property\".");
    }
    if (value == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "an enum option's target needs a \"value\".");
    }
    if (record->Property != NULL)
    {
        if ((record->Component != NULL) || (record->Axis != PrefabAxis_None))
        {
            return Error_Construct2(ErrorCode_InvalidJSON, "a nested property's target takes no \"component\" and no \"axis\".");
        }
        outTarget->ComponentType = (int32_t)WorldComponentType_Prefab;
        return CollectTargetObjects(self, record, outTarget->ComponentType, outTarget);
    }
    Error Result = FindTargetRow(self, record, outTarget, &outTarget->ComponentType);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = CollectTargetObjects(self, record, outTarget->ComponentType, outTarget);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((record->Axis != PrefabAxis_None) && !DoesRowTakeAxis(outTarget->Row, record->Axis))
    {
        return Error_Construct4(ErrorCode_InvalidJSON, "the row %s takes no such axis.", (const char*)record->Row);
    }
    Result = CheckRowValue(self, outTarget, value, pool);
    return (Result.Code == ErrorCode_Success) ? Result : WrapWhere("the row refuses the value", &Result);
}

/* A list of targets: a property's, each writing @p propertyValue, or an option's (NULL), each writing its own record's value. */
static Error ReadTargets(PrefabRead* read, const JSONObjectValue* array, const DataValue* propertyValue, const PrefabTarget** outTargets,
    size_t* outCount)
{
    Prefab* Self = read->Prefab;
    PropertyRecordList* List = NULL;
    Error Result = PropertyRecordList_ReadJSON(&TargetTable, array, PREFAB_MAX_TARGETS, NULL, &List);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    GenericBuffer_AddLast(&Self->_lists, &List);
    size_t Count = PropertyRecordList_GetCount(List);
    const PrefabTargetRecord* Records = PropertyRecordList_GetRecords(List);
    PrefabTarget* Targets = AllocateBlock(Self, Count * sizeof(PrefabTarget));
    for (size_t Index = 0U; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        const PrefabTargetRecord* Record = &Records[Index];
        if ((propertyValue != NULL) && (Record->Value != NULL))
        {
            Result = Error_Construct2(ErrorCode_InvalidJSON, "only an enum option's target takes a \"value\".");
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = ResolveTarget(Self, Record, (propertyValue != NULL) ? propertyValue : Record->Value, read->Pool, &Targets[Index]);
        }
        if (Result.Code != ErrorCode_Success)
        {
            char Where[64];
            snprintf(Where, sizeof(Where), "target %" PRIu64, (uint64_t)Index);
            Result = WrapWhere(Where, &Result);
        }
    }
    *outTargets = Targets;
    *outCount = Count;
    return Result;
}


// Static functions: properties.
static Error ReadOption(PrefabRead* read, const JSONCompoundEntry* entry)
{
    char Name[PRINT_CAPACITY];
    AssetJSON_CopyPrintable(entry->Key, Name, sizeof(Name));
    if (!NamespacedKey_IsValidLocalName(entry->Key))
    {
        return Error_Construct4(ErrorCode_InvalidJSON, "the option \"%s\" needs a local name ([a-z][a-z0-9_.-]*).", Name);
    }
    PrefabOption* Option = &read->Options[read->OptionCount];
    Option->Name = KeepText(read->Prefab, entry->Key);
    if (read->OptionDisplayNames != NULL)
    {
        JSONObjectValue DisplayName;
        bool IsFound = false;
        Error FindResult = JSONCompound_GetOptional(read->OptionDisplayNames, entry->Key, &DisplayName, &IsFound);
        if (FindResult.Code != ErrorCode_Success)
        {
            return FindResult;
        }
        if (IsFound && (DisplayName.Type == JSONValueType_String) && (DisplayName.Value.String != NULL)
            && (DisplayName.Value.String->_count <= PREFAB_TEXT_MAX_LENGTH))
        {
            unsigned char* Text = AllocateBlock(read->Prefab, DisplayName.Value.String->_count + 1U);
            if (DisplayName.Value.String->_count > 0U)
            {
                Memory_Copy(DisplayName.Value.String->_data, Text, DisplayName.Value.String->_count);
            }
            Option->DisplayName = Text;
        }
        else if (IsFound)
        {
            return Error_Construct4(ErrorCode_InvalidJSON, "the option \"%s\"'s display name must be a short text.", Name);
        }
    }
    Error Result = ReadTargets(read, &entry->Value, NULL, &Option->Targets, &Option->TargetCount);
    if (Result.Code != ErrorCode_Success)
    {
        char Where[PRINT_CAPACITY + 16U];
        snprintf(Where, sizeof(Where), "option \"%s\"", Name);
        return WrapWhere(Where, &Result);
    }
    read->OptionCount++;
    return Error_CreateSuccess();
}

/* A display name must name an option. */
static Error CheckOptionDisplayName(PrefabRead* read, const JSONCompoundEntry* entry)
{
    for (size_t Index = 0U; Index < read->OptionCount; Index++)
    {
        if (StringUTF8_GetByteLength(read->Options[Index].Name) == StringUTF8_GetByteLength(entry->Key)
            && Memory_IsEqual(read->Options[Index].Name, entry->Key, StringUTF8_GetByteLength(entry->Key)))
        {
            return Error_CreateSuccess();
        }
    }
    char Name[PRINT_CAPACITY];
    AssetJSON_CopyPrintable(entry->Key, Name, sizeof(Name));
    return Error_Construct4(ErrorCode_InvalidJSON, "\"option_display_names\" names \"%s\", which is no option.", Name);
}

static Error ReadOptions(PrefabRead* read, PrefabProperty* property, JSONCompound* block)
{
    JSONObjectValue Options;
    bool HasOptions = false;
    Error Result = JSONCompound_GetOptional(block, OPTIONS_KEY, &Options, &HasOptions);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!HasOptions || (Options.Type != JSONValueType_Compound) || (JSONCompound_GetEntryCount(Options.Value.Compound) == 0U))
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "an enum needs \"options\": an object of at least one option.");
    }
    size_t Count = JSONCompound_GetEntryCount(Options.Value.Compound);
    if (Count > PREFAB_MAX_OPTIONS)
    {
        return Error_Construct4(ErrorCode_InvalidJSON, "it has %" PRIu64 " options; at most %" PRIu64 " are allowed.",
            (uint64_t)Count, (uint64_t)PREFAB_MAX_OPTIONS);
    }
    JSONObjectValue DisplayNames;
    bool HasDisplayNames = false;
    Result = JSONCompound_GetOptional(block, OPTION_DISPLAY_NAMES_KEY, &DisplayNames, &HasDisplayNames);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (HasDisplayNames && (DisplayNames.Type != JSONValueType_Compound))
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "\"option_display_names\" must be an object.");
    }
    if (HasDisplayNames && (JSONCompound_GetEntryCount(DisplayNames.Value.Compound) > Count))
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "\"option_display_names\" names more than there are options.");
    }
    read->OptionDisplayNames = HasDisplayNames ? DisplayNames.Value.Compound : NULL;
    read->Options = AllocateBlock(read->Prefab, Count * sizeof(PrefabOption));
    read->OptionCount = 0U;
    Result = ForEachEntry(read, Options.Value.Compound, ReadOption);
    property->Options = read->Options;
    property->OptionCount = read->OptionCount;
    if ((Result.Code == ErrorCode_Success) && HasDisplayNames)
    {
        Result = ForEachEntry(read, DisplayNames.Value.Compound, CheckOptionDisplayName);
    }
    return Result;
}

/* The optional range, by what the block writes. */
static Error ReadRange(PrefabProperty* property, const PrefabPropertyRecord* record, JSONCompound* block)
{
    JSONObjectValue Unused;
    Error Result = JSONCompound_GetOptional(block, MINIMUM_KEY, &Unused, &property->HasMinimum);
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONCompound_GetOptional(block, MAXIMUM_KEY, &Unused, &property->HasMaximum);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    property->Minimum = record->Minimum;
    property->Maximum = record->Maximum;
    bool IsNumeric = (record->Type == PrefabPropertyType_Int) || (record->Type == PrefabPropertyType_Float)
        || (record->Type == PrefabPropertyType_Angle) || (record->Type == PrefabPropertyType_Vector)
        || (record->Type == PrefabPropertyType_Vector2);
    if ((property->HasMinimum || property->HasMaximum) && !IsNumeric)
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "only a number or a vector takes a range.");
    }
    if (property->HasMinimum && property->HasMaximum && (property->Minimum > property->Maximum))
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "its minimum lies above its maximum.");
    }
    return Error_CreateSuccess();
}

/* An asset property's asset type must be one the manager knows; no other type takes one. */
static Error CheckAssetType(const PrefabRead* read, const PrefabPropertyRecord* record)
{
    if (record->AssetType == NULL)
    {
        return Error_CreateSuccess();
    }
    if (record->Type != PrefabPropertyType_Asset)
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "only an asset property takes an \"asset_type\".");
    }
    AssetTypeID Type = ASSET_TYPE_ID_INVALID;
    Error Result = AssetManager_GetAssetTypeByName(read->Manager, record->AssetType, &Type);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (Type == ASSET_TYPE_ID_INVALID)
    {
        char Name[PRINT_CAPACITY];
        AssetJSON_CopyPrintable(record->AssetType, Name, sizeof(Name));
        return Error_Construct4(ErrorCode_InvalidJSON, "no asset type is named \"%s\".", Name);
    }
    return Result;
}

/* Everything after the name: the block's rows, its range, its options or targets, and its default against all of them. */
static Error ReadPropertyBlock(PrefabRead* read, PrefabProperty* property, PrefabPropertyRecord* record, JSONCompound* block)
{
    Error Result = AssetJSON_RejectUnknownKeys(block, BlockKeys, sizeof(BlockKeys) / sizeof(BlockKeys[0]), "a prefab's property");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    PropertyReadOptions Options = PropertyReadOptions_CreateDefault();
    Options.UnknownKeyMode = PropertyUnknownKeyMode_Ignore; // the block's other keys are read here, and the list above refuses the rest
    Options.Pool = read->Pool;
    Result = Properties_ReadJSON(&PropertyTable_, block, record, &Options);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (record->Default == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "a property needs a \"default\".");
    }
    JSONObjectValue WrittenType;
    bool HasType = false;
    Result = JSONCompound_GetOptional(block, TYPE_KEY, &WrittenType, &HasType);
    if ((Result.Code == ErrorCode_Success) && !HasType)
    {
        Result = Error_Construct2(ErrorCode_InvalidJSON, "a property needs a \"type\".");
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    property->Type = record->Type;
    property->DisplayName = record->DisplayName;
    property->Category = record->Category;
    property->Description = record->Description;
    property->AssetType = record->AssetType;
    property->Default = record->Default;
    Result = ReadRange(property, record, block);
    if (Result.Code == ErrorCode_Success)
    {
        Result = CheckAssetType(read, record);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    JSONObjectValue Targets;
    bool HasTargets = false;
    Result = JSONCompound_GetOptional(block, TARGETS_KEY, &Targets, &HasTargets);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (record->Type == PrefabPropertyType_Enum)
    {
        Result = HasTargets ? Error_Construct2(ErrorCode_InvalidJSON, "an enum's options write: it takes no \"targets\".")
            : ReadOptions(read, property, block);
    }
    else
    {
        JSONObjectValue Unused;
        bool HasOptions = false;
        Result = JSONCompound_GetOptional(block, OPTIONS_KEY, &Unused, &HasOptions);
        if ((Result.Code == ErrorCode_Success) && !HasOptions)
        {
            Result = JSONCompound_GetOptional(block, OPTION_DISPLAY_NAMES_KEY, &Unused, &HasOptions);
        }
        if ((Result.Code == ErrorCode_Success) && HasOptions)
        {
            Result = Error_Construct2(ErrorCode_InvalidJSON, "only an enum takes \"options\" and \"option_display_names\".");
        }
        // The value's type comes first, so a target's complaint is about the target.
        if (Result.Code == ErrorCode_Success)
        {
            Result = PrefabProperty_CheckValue(property, record->Default, read->Pool);
        }
        if ((Result.Code == ErrorCode_Success) && HasTargets)
        {
            Result = ReadTargets(read, &Targets, record->Default, &property->Targets, &property->TargetCount);
        }
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return PrefabProperty_CheckValue(property, record->Default, read->Pool);
}

static Error ReadProperty(PrefabRead* read, const JSONCompoundEntry* entry)
{
    Prefab* Self = read->Prefab;
    char Name[PRINT_CAPACITY];
    AssetJSON_CopyPrintable(entry->Key, Name, sizeof(Name));
    Error Result = Error_CreateSuccess();
    if (!NamespacedKey_IsValidLocalName(entry->Key))
    {
        Result = Error_Construct2(ErrorCode_InvalidJSON, "it needs a local name ([a-z][a-z0-9_.-]*).");
    }
    else if (entry->Value.Type != JSONValueType_Compound)
    {
        Result = Error_Construct2(ErrorCode_InvalidJSON, "it must be an object.");
    }
    PrefabProperty* Property = &Self->_properties[read->Index];
    PrefabPropertyRecord* Record = &Self->_records[read->Index];
    if (Result.Code == ErrorCode_Success)
    {
        Property->Name = KeepText(Self, entry->Key);
        Result = Properties_Construct(&PropertyTable_, Record);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Self->_propertyCount = read->Index + 1U; // from here it is released with the rest
        Result = ReadPropertyBlock(read, Property, Record, entry->Value.Value.Compound);
    }
    if (Result.Code != ErrorCode_Success)
    {
        char Where[PRINT_CAPACITY + 16U];
        snprintf(Where, sizeof(Where), "property \"%s\"", Name);
        return WrapWhere(Where, &Result);
    }
    read->Index++;
    return Error_CreateSuccess();
}

static Error ReadProperties(Prefab* self, JSONCompound* root, JSONObjectPool* pool, AssetManager* manager)
{
    JSONObjectValue Value;
    bool IsFound = false;
    Error Result = JSONCompound_GetOptional(root, PROPERTIES_KEY, &Value, &IsFound);
    if ((Result.Code != ErrorCode_Success) || !IsFound)
    {
        return Result;
    }
    if (Value.Type != JSONValueType_Compound)
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "\"properties\" must be an object.");
    }
    size_t Count = JSONCompound_GetEntryCount(Value.Value.Compound);
    if (Count > PREFAB_MAX_PROPERTIES)
    {
        return Error_Construct4(ErrorCode_InvalidJSON, "it declares %" PRIu64 " properties; at most %" PRIu64 " are allowed.",
            (uint64_t)Count, (uint64_t)PREFAB_MAX_PROPERTIES);
    }
    self->_properties = AllocateBlock(self, Count * sizeof(PrefabProperty));
    self->_records = AllocateBlock(self, Count * sizeof(PrefabPropertyRecord));
    PrefabRead Read = { .Prefab = self, .Pool = pool, .Manager = manager };
    return ForEachEntry(&Read, Value.Value.Compound, ReadProperty);
}


// Static functions: objects.
static ComparisonResult CompareNameEntries(GenericBuffer* buffer, GenericBufferElementData a, GenericBufferElementData b,
    const UserData* userData)
{
    (void)buffer;
    (void)userData;
    const PrefabNameEntry* First = a._element;
    const PrefabNameEntry* Second = b._element;
    size_t Shared = (First->ByteCount < Second->ByteCount) ? First->ByteCount : Second->ByteCount;
    for (size_t Index = 0U; Index < Shared; Index++)
    {
        if (First->Name[Index] != Second->Name[Index])
        {
            return (First->Name[Index] < Second->Name[Index]) ? ComparisonResult_ALessThanB : ComparisonResult_AGreaterThanB;
        }
    }
    if (First->ByteCount == Second->ByteCount)
    {
        return ComparisonResult_AEqualsB;
    }
    return (First->ByteCount < Second->ByteCount) ? ComparisonResult_ALessThanB : ComparisonResult_AGreaterThanB;
}

/* The objects parents first from the one root; each with a local name of its own. */
static Error ListObjects(Prefab* self)
{
    size_t Count = World_GetObjectCount(&self->_template);
    if (Count == 0U)
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "a prefab holds at least one object.");
    }
    WorldObject* Root = NULL;
    for (size_t Index = 0U; Index < Count; Index++)
    {
        WorldObject* Object = NULL;
        Error Result = World_GetObjectByIndex(&self->_template, Index, &Object);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        if (WorldObject_GetParent(Object) == NULL)
        {
            if (Root != NULL)
            {
                return Error_Construct2(ErrorCode_InvalidJSON, "a prefab has one root: every other object names a parent.");
            }
            Root = Object;
        }
    }
    if (Root == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "a prefab has one root.");
    }
    self->_objects = AllocateBlock(self, Count * sizeof(PrefabObject));
    self->_objectCount = 0U;
    // Depth first over the links: no recursion, parents before their children, children in the file's order. The stack holds the
    // index of the object each level hangs under: a hierarchy has at most WORLD_HIERARCHY_MAX_DEPTH levels.
    uint32_t Parents[WORLD_HIERARCHY_MAX_DEPTH + 1U];
    size_t Depth = 0U;
    Parents[0] = PREFAB_NO_PARENT;
    WorldObject* Current = Root;
    while (Current != NULL)
    {
        const unsigned char* Name = WorldObject_GetName(Current);
        if ((Name == NULL) || !NamespacedKey_IsValidLocalName(Name))
        {
            return Error_Construct4(ErrorCode_InvalidJSON, "object %" PRIu64 " needs a name: a local name ([a-z][a-z0-9_.-]*).",
                WorldObject_GetId(Current));
        }
        uint32_t Index = (uint32_t)self->_objectCount;
        self->_objects[Index] = (PrefabObject){ .Name = Name, .Template = Current, .Parent = Parents[Depth] };
        self->_objectCount++;
        WorldObject* Child = WorldObject_GetFirstChild(Current);
        if (Child != NULL)
        {
            Depth++;
            Parents[Depth] = Index;
            Current = Child;
            continue;
        }
        while ((Current != NULL) && (WorldObject_GetNextSibling(Current) == NULL))
        {
            Current = WorldObject_GetParent(Current);
            Depth = (Depth > 0U) ? (Depth - 1U) : 0U;
        }
        Current = (Current != NULL) ? WorldObject_GetNextSibling(Current) : NULL;
    }
    return Error_CreateSuccess();
}

/* The sorted index of the names, which also finds the same name twice. */
static Error IndexNames(Prefab* self)
{
    self->_nameOrder = AllocateBlock(self, self->_objectCount * sizeof(PrefabNameEntry));
    GenericBuffer Order;
    GenericBuffer_CreateConstant(&Order, self->_nameOrder, self->_objectCount * sizeof(PrefabNameEntry), sizeof(PrefabNameEntry),
        self->_objectCount);
    for (size_t Index = 0U; Index < self->_objectCount; Index++)
    {
        self->_nameOrder[Index] = (PrefabNameEntry){ .Name = self->_objects[Index].Name,
            .ByteCount = StringUTF8_GetByteLength(self->_objects[Index].Name), .Index = (uint32_t)Index };
    }
    if (self->_objectCount > 1U)
    {
        PrefabNameEntry Scratch[2];
        GenericBuffer_SortAscending(&Order, CompareNameEntries, NULL, Scratch);
    }
    for (size_t Index = 1U; Index < self->_objectCount; Index++)
    {
        const PrefabNameEntry* Before = &self->_nameOrder[Index - 1U];
        const PrefabNameEntry* Entry = &self->_nameOrder[Index];
        if ((Before->ByteCount == Entry->ByteCount) && Memory_IsEqual(Before->Name, Entry->Name, Entry->ByteCount))
        {
            char Name[PRINT_CAPACITY];
            AssetJSON_CopyPrintable(Entry->Name, Name, sizeof(Name));
            return Error_Construct4(ErrorCode_InvalidJSON, "two objects are named \"%s\": a name is unique inside a prefab.", Name);
        }
    }
    return Error_CreateSuccess();
}

static Error ReadObjects(Prefab* self, JSONCompound* root, JSONObjectPool* pool)
{
    JSONObjectValue Version;
    bool HasVersion = false;
    Error Result = JSONCompound_GetOptional(root, FORMAT_VERSION_KEY, &Version, &HasVersion);
    if ((Result.Code == ErrorCode_Success) && !HasVersion)
    {
        Result = Error_Construct2(ErrorCode_InvalidJSON, "\"format_version\" is missing: the world file format its objects are in.");
    }
    uint64_t FormatVersion = 0U;
    if (Result.Code == ErrorCode_Success)
    {
        Result = GameJSON_ValueToUInt64(&Version, &FormatVersion);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    WorldFileReadOptions Options = WorldFileReadOptions_Create(&self->_keys, NULL, NULL);
    Result = WorldFile_ReadObjectsJSON(root, FormatVersion, pool, &Options, PREFAB_MAX_OBJECTS, &self->_template);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    self->_hasTemplate = true;
    Result = ListObjects(self);
    return (Result.Code == ErrorCode_Success) ? IndexNames(self) : Result;
}


// Functions: rows.
bool PrefabInternal_IsRowWritable(const PropertyTable* table, const PropertyDescriptor* row)
{
    if (((row->Flags & (uint32_t)PropertyFlags_ReadOnly) != 0U) || (row->Type == PropertyType_DataMap))
    {
        return false;
    }
    return (table != WorldObject_GetPropertyTable()) || !WorldInternal_AreStringsEqual(row->Key, NAME_ROW_KEY);
}

Error PrefabInternal_ApplyRowValue(const PropertyTable* table, void* instance, const PropertyDescriptor* row, PrefabAxis axis,
    const DataValue* value, JSONObjectPool* pool)
{
    if (axis == PrefabAxis_None)
    {
        return Properties_SetFromDataValue(table, instance, row->Key, value, pool);
    }
    if (!IsNumber(value))
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "an axis takes a number.");
    }
    PropertyValue Current;
    Error Result = Properties_Get(table, instance, row->Key, &Current);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    double Number = GetNumber(value);
    bool HasX = (axis == PrefabAxis_X) || (axis == PrefabAxis_All);
    bool HasY = (axis == PrefabAxis_Y) || (axis == PrefabAxis_All);
    bool HasZ = (axis == PrefabAxis_Z) || (axis == PrefabAxis_All);
    switch (row->Type)
    {
        case PropertyType_Vector2:
            Current.Data.Vector2Value.x = HasX ? (float)Number : Current.Data.Vector2Value.x;
            Current.Data.Vector2Value.y = HasY ? (float)Number : Current.Data.Vector2Value.y;
            break;
        case PropertyType_AngleVector3:
            Number *= PREFAB_DEGREES_TO_RADIANS; // an angle's number is degrees, as its row's JSON form is
            Current.Data.Vector3Value.x = HasX ? (float)Number : Current.Data.Vector3Value.x;
            Current.Data.Vector3Value.y = HasY ? (float)Number : Current.Data.Vector3Value.y;
            Current.Data.Vector3Value.z = HasZ ? (float)Number : Current.Data.Vector3Value.z;
            break;
        case PropertyType_Vector3:
            Current.Data.Vector3Value.x = HasX ? (float)Number : Current.Data.Vector3Value.x;
            Current.Data.Vector3Value.y = HasY ? (float)Number : Current.Data.Vector3Value.y;
            Current.Data.Vector3Value.z = HasZ ? (float)Number : Current.Data.Vector3Value.z;
            break;
        case PropertyType_Vector3Double:
            Current.Data.Vector3DoubleValue.X = HasX ? Number : Current.Data.Vector3DoubleValue.X;
            Current.Data.Vector3DoubleValue.Y = HasY ? Number : Current.Data.Vector3DoubleValue.Y;
            Current.Data.Vector3DoubleValue.Z = HasZ ? Number : Current.Data.Vector3DoubleValue.Z;
            break;
        default:
            return Error_Construct2(ErrorCode_InvalidOperation, "only a vector row takes an axis.");
    }
    return Properties_Set(table, instance, row->Key, &Current);
}


// Functions: the file.
Error Prefab_Read(JSONCompound* root, size_t fileByteCount, JSONObjectPool* pool, AssetManager* manager, Prefab** outPrefab)
{
    if (outPrefab != NULL)
    {
        *outPrefab = NULL;
    }
    if ((root == NULL) || (pool == NULL) || (manager == NULL) || (outPrefab == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Prefab_Read: root, pool, manager and outPrefab must not be NULL.");
    }
    Error Result = AssetJSON_RejectUnknownKeys(root, RootKeys, sizeof(RootKeys) / sizeof(RootKeys[0]), "a prefab");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Prefab* Self = Memory_Allocate(sizeof(Prefab));
    Memory_Zero(Self, sizeof(*Self));
    Self->_fileByteCount = fileByteCount;
    GenericBuffer_AllocateVariable(&Self->_blocks, 0U, sizeof(void*));
    GenericBuffer_AllocateVariable(&Self->_lists, 0U, sizeof(PropertyRecordList*));
    GenericBuffer_AllocateVariable(&Self->_texts, 0U, sizeof(unsigned char*));
    Result = KeyRegistry_Construct(&Self->_keys);
    Self->_hasKeys = Result.Code == ErrorCode_Success;
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadObjects(Self, root, pool);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadProperties(Self, root, pool, manager);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Prefab_Destroy(Self);
        return Result;
    }
    *outPrefab = Self;
    return Error_CreateSuccess();
}

void Prefab_Destroy(Prefab* self)
{
    if (self == NULL)
    {
        return;
    }
    if (self->_hasTemplate)
    {
        Error Result = World_Deconstruct(&self->_template);
        Error_Deconstruct(&Result);
    }
    if (self->_hasKeys)
    {
        KeyRegistry_Deconstruct(&self->_keys);
    }
    for (size_t Index = 0U; Index < self->_propertyCount; Index++)
    {
        Properties_Deconstruct(&PropertyTable_, &self->_records[Index]);
    }
    PropertyRecordList* const* Lists = (PropertyRecordList* const*)self->_lists._data;
    for (size_t Index = 0U; Index < self->_lists._count; Index++)
    {
        PropertyRecordList_Destroy(Lists[Index]);
    }
    void* const* Blocks = (void* const*)self->_blocks._data;
    for (size_t Index = 0U; Index < self->_blocks._count; Index++)
    {
        Memory_Free(Blocks[Index]);
    }
    unsigned char* const* Texts = (unsigned char* const*)self->_texts._data;
    for (size_t Index = 0U; Index < self->_texts._count; Index++)
    {
        Memory_Free(Texts[Index]);
    }
    Memory_Free(self->_lists._data);
    Memory_Free(self->_blocks._data);
    Memory_Free(self->_texts._data);
    Memory_Free(self);
}

const PropertyTable* Prefab_GetTargetPropertyTable(void)
{
    return &TargetTable;
}

const PropertyTable* Prefab_GetPropertyPropertyTable(void)
{
    return &PropertyTable_;
}


// Functions: the objects.
size_t Prefab_GetObjectCount(const Prefab* self)
{
    return self->_objectCount;
}

const unsigned char* Prefab_GetObjectName(const Prefab* self, size_t index)
{
    return self->_objects[index].Name;
}

uint32_t Prefab_GetObjectParent(const Prefab* self, size_t index)
{
    return self->_objects[index].Parent;
}

const WorldObject* Prefab_GetObjectTemplate(const Prefab* self, size_t index)
{
    return self->_objects[index].Template;
}

size_t Prefab_GetFileByteCount(const Prefab* self)
{
    return self->_fileByteCount;
}

bool Prefab_TryFindObjectById(const Prefab* self, uint64_t id, uint32_t* outIndex)
{
    WorldObject* Found = NULL;
    if (!World_TryGetObject((World*)&self->_template, id, &Found))
    {
        return false;
    }
    const unsigned char* Name = WorldObject_GetName(Found);
    return Prefab_TryFindObject(self, Name, StringUTF8_GetByteLength(Name), outIndex);
}

bool Prefab_TryFindObject(const Prefab* self, const unsigned char* name, size_t byteCount, uint32_t* outIndex)
{
    size_t Low = 0U;
    size_t High = self->_objectCount;
    while (Low < High)
    {
        size_t Middle = Low + ((High - Low) / 2U);
        const PrefabNameEntry* Entry = &self->_nameOrder[Middle];
        size_t Shared = (Entry->ByteCount < byteCount) ? Entry->ByteCount : byteCount;
        int Order = 0;
        for (size_t Index = 0U; (Index < Shared) && (Order == 0); Index++)
        {
            Order = (int)Entry->Name[Index] - (int)name[Index];
        }
        if (Order == 0)
        {
            Order = (Entry->ByteCount == byteCount) ? 0 : ((Entry->ByteCount < byteCount) ? -1 : 1);
        }
        if (Order == 0)
        {
            *outIndex = Entry->Index;
            return true;
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
    return false;
}

const KeyRegistry* Prefab_GetKeys(const Prefab* self)
{
    return &self->_keys;
}


// Functions: the properties.
size_t Prefab_GetPropertyCount(const Prefab* self)
{
    return self->_propertyCount;
}

const PrefabProperty* Prefab_GetProperty(const Prefab* self, size_t index)
{
    return &self->_properties[index];
}

const PrefabProperty* Prefab_FindProperty(const Prefab* self, const unsigned char* name)
{
    for (size_t Index = 0U; Index < self->_propertyCount; Index++)
    {
        if (WorldInternal_AreStringsEqual(self->_properties[Index].Name, name))
        {
            return &self->_properties[Index];
        }
    }
    return NULL;
}

const PrefabOption* PrefabProperty_FindOption(const PrefabProperty* property, const unsigned char* name)
{
    for (size_t Index = 0U; Index < property->OptionCount; Index++)
    {
        if (WorldInternal_AreStringsEqual(property->Options[Index].Name, name))
        {
            return &property->Options[Index];
        }
    }
    return NULL;
}

Error PrefabProperty_CheckValue(const PrefabProperty* property, const DataValue* value, JSONObjectPool* pool)
{
    switch (property->Type)
    {
        case PrefabPropertyType_Bool:
            return (value->Type == DataType_Bool) ? Error_CreateSuccess()
                : Error_Construct2(ErrorCode_ArgumentOutOfRange, "the value must be true or false.");
        case PrefabPropertyType_Int:
            return (value->Type == DataType_Int) ? CheckRange(property, (double)value->IntValue)
                : Error_Construct2(ErrorCode_ArgumentOutOfRange, "the value must be a whole number.");
        case PrefabPropertyType_Float:
        case PrefabPropertyType_Angle:
            return IsNumber(value) ? CheckRange(property, GetNumber(value))
                : Error_Construct2(ErrorCode_ArgumentOutOfRange, "the value must be a number.");
        case PrefabPropertyType_Color:
            return CheckColor(value, pool);
        case PrefabPropertyType_Vector:
            return CheckVector(property, value, 3U);
        case PrefabPropertyType_Vector2:
            return CheckVector(property, value, 2U);
        case PrefabPropertyType_Enum:
            if ((value->Type == DataType_String) && (PrefabProperty_FindOption(property, value->StringValue) != NULL))
            {
                return Error_CreateSuccess();
            }
            return Error_Construct2(ErrorCode_ArgumentOutOfRange, "the value must name one of its options.");
        case PrefabPropertyType_Asset:
            return ((value->Type == DataType_String) && AssetManager_IsValidAssetName(value->StringValue))
                ? Error_CreateSuccess() : Error_Construct2(ErrorCode_ArgumentOutOfRange, "the value must be an asset's name.");
        case PrefabPropertyType_String:
            return (value->Type == DataType_String) ? Error_CreateSuccess()
                : Error_Construct2(ErrorCode_ArgumentOutOfRange, "the value must be a text.");
    }
    return Error_Construct2(ErrorCode_InvalidState, "the property has an unknown type.");
}
