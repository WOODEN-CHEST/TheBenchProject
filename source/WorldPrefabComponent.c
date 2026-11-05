#include "WorldPrefabComponent.h"
#include "NamespacedKey.h"
#include "WorldInternal.h"
#include "wr/WRMemory.h"


// Macros.
/** The format version of the table. */
#define PREFAB_COMPONENT_TABLE_VERSION 1U
#define CATEGORY_PREFAB ((const unsigned char*)u8"Prefab")
#define VALUES_KEY ((const unsigned char*)u8"values")
#define OVERRIDES_KEY ((const unsigned char*)u8"object_overrides")


// Fields.
/* The wire ids are the save schema: never renumber or reuse one. The next free one is 4. */
static const PropertyDescriptor PrefabComponentProperties[] =
{
    {
        .Key = u8"prefab", .WireId = 1U, .Type = PropertyType_AssetName, PROPERTY_MEMBER(WorldPrefabComponent, _prefab),
        .Default.StringValue = NULL, .Rules = PROPERTY_RULES_STRING(WORLD_PREFAB_COMPONENT_NAME_MAX_LENGTH, true),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_PREFAB, .DisplayName = u8"Prefab",
        .Description = u8"The prefab this object is a copy of: its objects are made under this object. None makes nothing.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"values", .WireId = 2U, .Type = PropertyType_DataValue, PROPERTY_MEMBER(WorldPrefabComponent, _values),
        .Rules = PROPERTY_RULES_DATA_VALUE(WORLD_PREFAB_COMPONENT_VALUES_MAX_BYTES, true), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_PREFAB, .DisplayName = u8"Values",
        .Description = u8"This copy's values of the prefab's properties, by name. What it leaves out takes the prefab's default.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"object_overrides", .WireId = 3U, .Type = PropertyType_DataValue,
        PROPERTY_MEMBER(WorldPrefabComponent, _objectOverrides),
        .Rules = PROPERTY_RULES_DATA_VALUE(WORLD_PREFAB_COMPONENT_OVERRIDES_MAX_BYTES, true), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_PREFAB, .DisplayName = u8"Object overrides",
        .Description = u8"What this copy changes in the prefab's objects, by their names: is_removed, rows of the object, rows of its "
            u8"components under components.",
        .SinceVersion = 1U
    },
};

static const PropertyTable PrefabComponentTable =
{
    .Name = u8"prefab_component",
    .Version = PREFAB_COMPONENT_TABLE_VERSION,
    .StructSize = sizeof(WorldPrefabComponent),
    .Descriptors = PrefabComponentProperties,
    .DescriptorCount = sizeof(PrefabComponentProperties) / sizeof(PrefabComponentProperties[0]),
    .AfterSet = WorldInternal_ComponentAfterSet
};


// Static functions.
static Error ConstructPrefabComponent(WorldComponent* component)
{
    return Properties_Construct(&PrefabComponentTable, component);
}

static void DeconstructPrefabComponent(WorldComponent* component)
{
    Properties_Deconstruct(&PrefabComponentTable, component);
}

static const WorldComponentTypeInfo PrefabComponentTypeInfo =
{
    .Type = WorldComponentType_Prefab,
    .Name = u8"prefab",
    .Size = sizeof(WorldPrefabComponent),
    .Table = &PrefabComponentTable,
    .IsTransformDriver = false,
    .Construct = ConstructPrefabComponent,
    .Deconstruct = DeconstructPrefabComponent,
    .OnAttach = NULL,
    .OnDetach = NULL,
    .OnPropertySet = NULL
};

/* A copy of a compound row's value to change, an empty compound for none. */
static Error CloneCompound(const DataValue* value, DataValue* outCopy)
{
    if (value != NULL)
    {
        *outCopy = DataValue_Clone(value);
        return Error_CreateSuccess();
    }
    return DataValue_CreateCompound(outCopy);
}

/* Writes a compound row through the table; one left with nothing goes back to none. Takes @p value. */
static Error SetCompoundRow(WorldPrefabComponent* self, const unsigned char* key, DataValue* value)
{
    PropertyValue Row = PropertyValue_FromDataValue((DataValue_GetCompoundCount(value) > 0U) ? value : NULL);
    Error Result = Properties_Set(&PrefabComponentTable, self, key, &Row);
    DataValue_Deconstruct(value);
    return Result;
}

/* Sets @p name in a compound to a copy of @p value, or takes it out for NULL. */
static Error SetEntry(DataValue* compound, const unsigned char* name, const DataValue* value)
{
    if (value == NULL)
    {
        DataValue_CompoundRemove(compound, name);
        return Error_CreateSuccess();
    }
    DataValue Copy = DataValue_Clone(value);
    Error Result = DataValue_CompoundSet(compound, name, &Copy);
    DataValue_Deconstruct(&Copy);
    return Result;
}

/* Sets a row in a block's compound by a chain of names, making what the chain lacks; emptied levels go. */
static Error SetInBlock(DataValue* block, const unsigned char* component, const unsigned char* row, const DataValue* value)
{
    if (component == NULL)
    {
        return SetEntry(block, row, value);
    }
    DataValue Components = DataValue_CreateTag();
    Error Result = CloneCompound(DataValue_FindCompoundEntry(block, WORLD_PREFAB_OVERRIDE_COMPONENTS_KEY), &Components);
    DataValue Rows = DataValue_CreateTag();
    if (Result.Code == ErrorCode_Success)
    {
        Result = CloneCompound(DataValue_FindCompoundEntry(&Components, component), &Rows);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = SetEntry(&Rows, row, value);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = SetEntry(&Components, component, (DataValue_GetCompoundCount(&Rows) > 0U) ? &Rows : NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = SetEntry(block, WORLD_PREFAB_OVERRIDE_COMPONENTS_KEY,
            (DataValue_GetCompoundCount(&Components) > 0U) ? &Components : NULL);
    }
    DataValue_Deconstruct(&Rows);
    DataValue_Deconstruct(&Components);
    return Result;
}

/* Writes one change of a path's block. */
static Error ChangeBlock(WorldPrefabComponent* self, const unsigned char* path, const unsigned char* component,
    const unsigned char* row, const DataValue* value)
{
    DataValue Overrides = DataValue_CreateTag();
    Error Result = CloneCompound(self->_objectOverrides, &Overrides);
    DataValue Block = DataValue_CreateTag();
    if (Result.Code == ErrorCode_Success)
    {
        Result = CloneCompound(DataValue_FindCompoundEntry(&Overrides, path), &Block);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = SetInBlock(&Block, component, row, value);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = SetEntry(&Overrides, path, (DataValue_GetCompoundCount(&Block) > 0U) ? &Block : NULL);
    }
    DataValue_Deconstruct(&Block);
    if (Result.Code != ErrorCode_Success)
    {
        DataValue_Deconstruct(&Overrides);
        return Result;
    }
    return SetCompoundRow(self, OVERRIDES_KEY, &Overrides);
}


// Functions.
const WorldComponentTypeInfo* WorldPrefabComponent_GetTypeInfo(void)
{
    return &PrefabComponentTypeInfo;
}

const PropertyTable* WorldPrefabComponent_GetPropertyTable(void)
{
    return &PrefabComponentTable;
}

Error WorldPrefabComponent_SetPrefab(WorldPrefabComponent* self, const unsigned char* prefab)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldPrefabComponent_SetPrefab: self must not be NULL.");
    }
    PropertyValue Value = PropertyValue_FromAssetName(prefab);
    return Properties_Set(&PrefabComponentTable, self, (const unsigned char*)u8"prefab", &Value);
}

const DataValue* WorldPrefabComponent_GetValue(const WorldPrefabComponent* self, const unsigned char* name)
{
    return (self->_values != NULL) ? DataValue_FindCompoundEntry(self->_values, name) : NULL;
}

Error WorldPrefabComponent_SetValue(WorldPrefabComponent* self, const unsigned char* name, const DataValue* value)
{
    if ((self == NULL) || (name == NULL) || !NamespacedKey_IsValidLocalName(name))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldPrefabComponent_SetValue: self and a local name are required.");
    }
    DataValue Values = DataValue_CreateTag();
    Error Result = CloneCompound(self->_values, &Values);
    if (Result.Code == ErrorCode_Success)
    {
        Result = SetEntry(&Values, name, value);
    }
    if (Result.Code != ErrorCode_Success)
    {
        DataValue_Deconstruct(&Values);
        return Result;
    }
    return SetCompoundRow(self, VALUES_KEY, &Values);
}

const DataValue* WorldPrefabComponent_GetObjectOverride(const WorldPrefabComponent* self, const unsigned char* path)
{
    return (self->_objectOverrides != NULL) ? DataValue_FindCompoundEntry(self->_objectOverrides, path) : NULL;
}

Error WorldPrefabComponent_SetObjectRow(WorldPrefabComponent* self, const unsigned char* path, const unsigned char* component,
    const unsigned char* row, const DataValue* value)
{
    if ((self == NULL) || (path == NULL) || (row == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldPrefabComponent_SetObjectRow: self, path and row are required.");
    }
    return ChangeBlock(self, path, component, row, value);
}

Error WorldPrefabComponent_SetObjectRemoved(WorldPrefabComponent* self, const unsigned char* path, bool isRemoved)
{
    if ((self == NULL) || (path == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldPrefabComponent_SetObjectRemoved: self and path are required.");
    }
    DataValue Flag = DataValue_FromBool(isRemoved);
    bool IsNested = false;
    for (const unsigned char* Character = path; *Character != 0U; Character++)
    {
        IsNested = IsNested || (*Character == (unsigned char)WORLD_PREFAB_PATH_SEPARATOR);
    }
    return ChangeBlock(self, path, NULL, WORLD_PREFAB_OVERRIDE_IS_REMOVED_KEY, (isRemoved || IsNested) ? &Flag : NULL);
}

Error WorldPrefabComponent_ClearObjectOverride(WorldPrefabComponent* self, const unsigned char* path)
{
    if ((self == NULL) || (path == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldPrefabComponent_ClearObjectOverride: self and path are required.");
    }
    if (DataValue_FindCompoundEntry(self->_objectOverrides, path) == NULL)
    {
        return Error_CreateSuccess();
    }
    DataValue Overrides = DataValue_Clone(self->_objectOverrides);
    DataValue_CompoundRemove(&Overrides, path);
    return SetCompoundRow(self, OVERRIDES_KEY, &Overrides);
}
