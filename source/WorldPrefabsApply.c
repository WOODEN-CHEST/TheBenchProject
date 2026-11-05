// What a copy's values and overrides come to (WorldPrefabsInternal.h): a list of writes, worked out row by row on a zeroed scratch
// instance from the template's value, and only what comes out different from the real row written.

#include <inttypes.h>
#include "WorldPrefabsInternal.h"
#include "WorldInternal.h"
#include "WorldObject.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"


// Macros.
/** The rows of a prefab component that nested copies receive. */
#define VALUES_ROW_KEY ((const unsigned char*)u8"values")
#define OVERRIDES_ROW_KEY ((const unsigned char*)u8"object_overrides")


// Types.
/* What a write does to its row. */
typedef enum PrefabWriteKindEnum
{
    /* Writes the value through the row's reader. */
    PrefabWriteKind_Row,
    /* Sets a nested copy's property in its values row. */
    PrefabWriteKind_NestedProperty,
    /* Merges an override block into a nested copy's overrides row. */
    PrefabWriteKind_NestedOverride,
    /* Writes nothing: the row was touched last time, so it comes back to the template's unless something else writes it. */
    PrefabWriteKind_Restore
} PrefabWriteKind;

/* One write of an apply. */
typedef struct PrefabWriteStruct
{
    uint32_t Object;
    int32_t Component;
    const PropertyTable* Table;
    const PropertyDescriptor* Row;
    PrefabWriteKind Kind;
    PrefabAxis Axis;
    /* Borrowed from the prefab or the component. */
    const DataValue* Value;
    /* A nested property's name, or the rest of a nested override's path; borrowed as well. */
    const unsigned char* Name;
    /* The order they were added in, which is the order they apply in. */
    uint32_t Sequence;
} PrefabWrite;


// Static functions: the list.
static void AddWrite(WorldPrefabs* self, const PrefabWrite* write)
{
    PrefabWrite Copy = *write;
    Copy.Sequence = (uint32_t)self->_writes._count;
    GenericBuffer_AddLast(&self->_writes, &Copy);
}

/* The rows of a prefab component's table that nested copies receive. */
static const PropertyDescriptor* FindPrefabRow(const unsigned char* key)
{
    return PropertyTable_FindByKey(WorldPrefabComponent_GetPropertyTable(), key);
}

/* What one target writes, on every object it reaches that the copy holds. @p valueBytes is what the value counts. */
static void AddTargetWrites(WorldPrefabs* self, const PrefabCopyState* state, const PrefabTarget* target, const DataValue* value, size_t valueBytes)
{
    for (size_t Index = 0U; Index < target->ObjectCount; Index++)
    {
        if (state->ObjectIds[target->Objects[Index]] == 0U)
        {
            continue;
        }
        self->_writeBytes += valueBytes;
        PrefabWrite Write = { .Object = target->Objects[Index], .Component = target->ComponentType, .Value = value };
        if (target->Row != NULL)
        {
            Write.Table = target->Table;
            Write.Row = target->Row;
            Write.Kind = PrefabWriteKind_Row;
            Write.Axis = target->Record->Axis;
        }
        else
        {
            Write.Table = WorldPrefabComponent_GetPropertyTable();
            Write.Row = FindPrefabRow(VALUES_ROW_KEY);
            Write.Kind = PrefabWriteKind_NestedProperty;
            Write.Name = target->Record->Property;
        }
        AddWrite(self, &Write);
    }
}

/* The value a property takes on this copy: its own if it is good, else the default. */
static const DataValue* GetPropertyValue(WorldPrefabs* self, PrefabCopyState* state, const WorldPrefabComponent* component,
    const PrefabProperty* property)
{
    const DataValue* Value = WorldPrefabComponent_GetValue(component, property->Name);
    if (Value == NULL)
    {
        return property->Default;
    }
    Error Result = PrefabProperty_CheckValue(property, Value, WorldPrefabsInternal_GetPool(self));
    if (Result.Code != ErrorCode_Success)
    {
        WorldPrefabsInternal_Warn(self, state, "gives the property \"%s\" a bad value, so it takes its default: %s",
            (const char*)property->Name, (Result.Message != NULL) ? (const char*)Result.Message : "");
        Error_Deconstruct(&Result);
        return property->Default;
    }
    return Value;
}

/* What every property writes, in the order declared; a value naming no property is warned about. */
static void AddPropertyWrites(WorldPrefabs* self, PrefabCopyState* state, const WorldPrefabComponent* component)
{
    size_t Count = Prefab_GetPropertyCount(state->Asset);
    for (size_t Index = 0U; Index < Count; Index++)
    {
        const PrefabProperty* Property = Prefab_GetProperty(state->Asset, Index);
        const DataValue* Value = GetPropertyValue(self, state, component, Property);
        if (Property->Type == PrefabPropertyType_Enum)
        {
            const PrefabOption* Option = PrefabProperty_FindOption(Property, Value->StringValue);
            for (size_t Target = 0U; (Option != NULL) && (Target < Option->TargetCount); Target++)
            {
                const DataValue* OptionValue = Option->Targets[Target].Record->Value;
                AddTargetWrites(self, state, &Option->Targets[Target], OptionValue, DataValue_GetByteSize(OptionValue, NULL));
            }
            continue;
        }
        size_t ValueBytes = DataValue_GetByteSize(Value, NULL);
        for (size_t Target = 0U; Target < Property->TargetCount; Target++)
        {
            AddTargetWrites(self, state, &Property->Targets[Target], Value, ValueBytes);
        }
    }
    for (size_t Index = 0U; Index < DataValue_GetCompoundCount(component->_values); Index++)
    {
        const unsigned char* Name = DataValue_GetCompoundEntryName(component->_values, Index);
        if (Prefab_FindProperty(state->Asset, Name) == NULL)
        {
            WorldPrefabsInternal_Warn(self, state, "sets a property its prefab no longer has: \"%s\".", (const char*)Name);
        }
    }
}

/* An override's row: found, writable, added; anything else warned about and ignored. */
static void AddOverrideRow(WorldPrefabs* self, PrefabCopyState* state, uint32_t object, int32_t componentType, const PropertyTable* table,
    const unsigned char* key, const DataValue* value)
{
    const PropertyDescriptor* Row = PropertyTable_FindByKey(table, key);
    if (Row == NULL)
    {
        WorldPrefabsInternal_Warn(self, state, "overrides a row that does not exist: %s.%s.", (const char*)table->Name, (const char*)key);
        return;
    }
    if (!PrefabInternal_IsRowWritable(table, Row))
    {
        WorldPrefabsInternal_Warn(self, state, "overrides a row a copy cannot write: %s.%s.", (const char*)table->Name, (const char*)key);
        return;
    }
    PrefabWrite Write = { .Object = object, .Component = componentType, .Table = table, .Row = Row, .Kind = PrefabWriteKind_Row,
        .Axis = PrefabAxis_None, .Value = value };
    AddWrite(self, &Write);
    self->_writeBytes += DataValue_GetByteSize(value, NULL);
}

/* The rows of an override's components section. */
static void AddOverrideComponents(WorldPrefabs* self, PrefabCopyState* state, uint32_t object, const DataValue* components)
{
    const WorldObject* Template = Prefab_GetObjectTemplate(state->Asset, object);
    if (components->Type != DataType_Compound)
    {
        WorldPrefabsInternal_Warn(self, state, "has a components section that is no object, on the object \"%s\".",
            (const char*)Prefab_GetObjectName(state->Asset, object));
        return;
    }
    for (size_t Index = 0U; Index < DataValue_GetCompoundCount(components); Index++)
    {
        const unsigned char* Name = DataValue_GetCompoundEntryName(components, Index);
        const DataValue* Rows = DataValue_GetCompoundEntryValue(components, Index);
        WorldComponentType Type = WorldComponentType_Model;
        if (!World_TryFindComponentType(self->_world, Name, &Type) || (WorldObject_GetComponent(Template, Type) == NULL))
        {
            WorldPrefabsInternal_Warn(self, state, "overrides a component the object \"%s\" does not have: \"%s\".",
                (const char*)Prefab_GetObjectName(state->Asset, object), (const char*)Name);
            continue;
        }
        if (Rows->Type != DataType_Compound)
        {
            WorldPrefabsInternal_Warn(self, state, "overrides the component \"%s\" of the object \"%s\" with what is no object.", (const char*)Name,
                (const char*)Prefab_GetObjectName(state->Asset, object));
            continue;
        }
        const PropertyTable* Table = World_GetComponentTypeInfo(self->_world, Type)->Table;
        for (size_t Row = 0U; Row < DataValue_GetCompoundCount(Rows); Row++)
        {
            AddOverrideRow(self, state, object, (int32_t)Type, Table, DataValue_GetCompoundEntryName(Rows, Row),
                DataValue_GetCompoundEntryValue(Rows, Row));
        }
    }
}

/* What every override writes. */
static void AddOverrideWrites(WorldPrefabs* self, PrefabCopyState* state, const WorldPrefabComponent* component)
{
    const DataValue* Overrides = component->_objectOverrides;
    for (size_t Index = 0U; Index < DataValue_GetCompoundCount(Overrides); Index++)
    {
        const unsigned char* Path = DataValue_GetCompoundEntryName(Overrides, Index);
        const DataValue* Block = DataValue_GetCompoundEntryValue(Overrides, Index);
        size_t Length = 0U;
        while ((Path[Length] != 0U) && (Path[Length] != WORLD_PREFAB_PATH_SEPARATOR))
        {
            Length++;
        }
        uint32_t Object = 0U;
        if ((Block->Type != DataType_Compound) || !Prefab_TryFindObject(state->Asset, Path, Length, &Object) || (state->ObjectIds[Object] == 0U))
        {
            continue; // WorldPrefabsApply_FindRemoved warned about a name it does not have
        }
        const WorldObject* Template = Prefab_GetObjectTemplate(state->Asset, Object);
        if (Path[Length] != 0U)
        {
            if (WorldObject_GetComponent(Template, WorldComponentType_Prefab) == NULL)
            {
                WorldPrefabsInternal_Warn(self, state, "overrides through an object that holds no copy: \"%s\".", (const char*)Path);
                continue;
            }
            PrefabWrite Write = { .Object = Object, .Component = (int32_t)WorldComponentType_Prefab,
                .Table = WorldPrefabComponent_GetPropertyTable(), .Row = FindPrefabRow(OVERRIDES_ROW_KEY),
                .Kind = PrefabWriteKind_NestedOverride, .Value = Block, .Name = Path + Length + 1U };
            AddWrite(self, &Write);
            self->_writeBytes += DataValue_GetByteSize(Block, NULL);
            continue;
        }
        for (size_t Entry = 0U; Entry < DataValue_GetCompoundCount(Block); Entry++)
        {
            const unsigned char* Key = DataValue_GetCompoundEntryName(Block, Entry);
            const DataValue* Value = DataValue_GetCompoundEntryValue(Block, Entry);
            if (WorldInternal_AreStringsEqual(Key, WORLD_PREFAB_OVERRIDE_IS_REMOVED_KEY))
            {
                continue;
            }
            if (WorldInternal_AreStringsEqual(Key, WORLD_PREFAB_OVERRIDE_COMPONENTS_KEY))
            {
                AddOverrideComponents(self, state, Object, Value);
                continue;
            }
            AddOverrideRow(self, state, Object, PREFAB_NO_COMPONENT, WorldObject_GetPropertyTable(), Key, Value);
        }
    }
}

/* Keeps a row touched last time in the list, so it comes back to the template's if nothing writes it now. */
static void AddRestores(WorldPrefabs* self, const PrefabCopyState* state)
{
    const PrefabTouchedRow* Touched = (const PrefabTouchedRow*)state->Touched._data;
    for (size_t Index = 0U; Index < state->Touched._count; Index++)
    {
        PrefabWrite Write = { .Object = Touched[Index].Object, .Component = Touched[Index].Component, .Table = Touched[Index].Table,
            .Row = Touched[Index].Row, .Kind = PrefabWriteKind_Restore };
        AddWrite(self, &Write);
    }
}

static ComparisonResult CompareWrites(GenericBuffer* buffer, GenericBufferElementData a, GenericBufferElementData b, const UserData* userData)
{
    (void)buffer;
    (void)userData;
    const PrefabWrite* First = a._element;
    const PrefabWrite* Second = b._element;
    if (First->Object != Second->Object)
    {
        return (First->Object < Second->Object) ? ComparisonResult_ALessThanB : ComparisonResult_AGreaterThanB;
    }
    if (First->Component != Second->Component)
    {
        return (First->Component < Second->Component) ? ComparisonResult_ALessThanB : ComparisonResult_AGreaterThanB;
    }
    if (First->Row != Second->Row)
    {
        return ((uintptr_t)First->Row < (uintptr_t)Second->Row) ? ComparisonResult_ALessThanB : ComparisonResult_AGreaterThanB;
    }
    if (First->Sequence != Second->Sequence)
    {
        return (First->Sequence < Second->Sequence) ? ComparisonResult_ALessThanB : ComparisonResult_AGreaterThanB;
    }
    return ComparisonResult_AEqualsB;
}


// Static functions: nested copies' rows.
/* The compound a nested copy's row holds on the scratch instance, to change, or an empty one. */
static DataValue CloneRowCompound(const PropertyTable* table, const void* scratch, const PropertyDescriptor* row)
{
    PropertyValue Current;
    Error Result = Properties_Get(table, scratch, row->Key, &Current);
    Error_Deconstruct(&Result);
    DataValue Copy = DataValue_CreateTag();
    if (Current.Data.DataValueValue != NULL)
    {
        return DataValue_Clone(Current.Data.DataValueValue);
    }
    Result = DataValue_CreateCompound(&Copy);
    Error_Deconstruct(&Result);
    return Copy;
}

/* Sets @p name in a compound to a copy of @p value. */
static Error SetClone(DataValue* compound, const unsigned char* name, const DataValue* value)
{
    DataValue Copy = DataValue_Clone(value);
    Error Result = DataValue_CompoundSet(compound, name, &Copy);
    DataValue_Deconstruct(&Copy);
    return Result;
}

/* An override block over another, name by name, a components section merged by component and row. */
static Error MergeBlock(DataValue* block, const DataValue* over)
{
    Error Result = Error_CreateSuccess();
    for (size_t Index = 0U; (Index < DataValue_GetCompoundCount(over)) && (Result.Code == ErrorCode_Success); Index++)
    {
        const unsigned char* Key = DataValue_GetCompoundEntryName(over, Index);
        const DataValue* Value = DataValue_GetCompoundEntryValue(over, Index);
        const DataValue* Existing = DataValue_FindCompoundEntry(block, Key);
        if (!WorldInternal_AreStringsEqual(Key, WORLD_PREFAB_OVERRIDE_COMPONENTS_KEY) || (Existing == NULL)
            || (Existing->Type != DataType_Compound) || (Value->Type != DataType_Compound))
        {
            Result = SetClone(block, Key, Value);
            continue;
        }
        DataValue Components = DataValue_Clone(Existing);
        for (size_t Entry = 0U; (Entry < DataValue_GetCompoundCount(Value)) && (Result.Code == ErrorCode_Success); Entry++)
        {
            const unsigned char* Name = DataValue_GetCompoundEntryName(Value, Entry);
            const DataValue* Rows = DataValue_GetCompoundEntryValue(Value, Entry);
            const DataValue* Old = DataValue_FindCompoundEntry(&Components, Name);
            if ((Old == NULL) || (Old->Type != DataType_Compound) || (Rows->Type != DataType_Compound))
            {
                Result = SetClone(&Components, Name, Rows);
                continue;
            }
            DataValue Merged = DataValue_Clone(Old);
            for (size_t Row = 0U; (Row < DataValue_GetCompoundCount(Rows)) && (Result.Code == ErrorCode_Success); Row++)
            {
                Result = SetClone(&Merged, DataValue_GetCompoundEntryName(Rows, Row), DataValue_GetCompoundEntryValue(Rows, Row));
            }
            if (Result.Code == ErrorCode_Success)
            {
                Result = DataValue_CompoundSet(&Components, Name, &Merged);
            }
            DataValue_Deconstruct(&Merged);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = DataValue_CompoundSet(block, Key, &Components);
        }
        DataValue_Deconstruct(&Components);
    }
    return Result;
}

/* What a name adds to a compound's byte count (DataValue_GetByteSize). */
static size_t GetEntryBytes(const unsigned char* name, const DataValue* value)
{
    return StringUTF8_GetByteLength(name) + 1U + DataValue_GetByteSize(value, NULL);
}

/* A nested copy's row with one more name, a property's value or an override's block over what the template gave that path, as long as the
 * row stays within what it holds: @p bytes follows what it counts, so no write walks all the others. */
static Error AddToNestedRow(DataValue* row, size_t* bytes, size_t maxBytes, const PrefabWrite* write)
{
    const DataValue* Existing = DataValue_FindCompoundEntry(row, write->Name);
    size_t OldBytes = (Existing != NULL) ? GetEntryBytes(write->Name, Existing) : 0U;
    DataValue Entry = DataValue_CreateTag();
    Error Result = Error_CreateSuccess();
    if ((write->Kind == PrefabWriteKind_NestedOverride) && (Existing != NULL) && (Existing->Type == DataType_Compound))
    {
        Entry = DataValue_Clone(Existing);
        Result = MergeBlock(&Entry, write->Value);
    }
    else
    {
        Entry = DataValue_Clone(write->Value);
    }
    size_t NewBytes = (Result.Code == ErrorCode_Success) ? (*bytes - OldBytes + GetEntryBytes(write->Name, &Entry)) : 0U;
    if ((Result.Code == ErrorCode_Success) && (NewBytes > maxBytes))
    {
        Result = Error_Construct4(ErrorCode_InvalidOperation, "a nested copy's row holds at most %" PRIu64 " bytes.", (uint64_t)maxBytes);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = DataValue_CompoundSet(row, write->Name, &Entry);
    }
    if (Result.Code == ErrorCode_Success)
    {
        *bytes = NewBytes;
    }
    DataValue_Deconstruct(&Entry);
    return Result;
}

/* A run of writes to a nested copy's row, on one working copy that is set once: a row of thousands of names costs each write its own size. */
static void ApplyNestedWrites(WorldPrefabs* self, PrefabCopyState* state, const PrefabWrite* writes, size_t count)
{
    const PropertyDescriptor* RowDescriptor = writes[0].Row;
    DataValue Row = CloneRowCompound(writes[0].Table, self->_scratch, RowDescriptor);
    size_t Bytes = DataValue_GetByteSize(&Row, NULL);
    for (size_t Index = 0U; Index < count; Index++)
    {
        Error Result = AddToNestedRow(&Row, &Bytes, RowDescriptor->Rules.MaxLength, &writes[Index]);
        if (Result.Code != ErrorCode_Success)
        {
            WorldPrefabsInternal_Warn(self, state, "writes what a row refuses, in the object \"%s\": %s",
                (const char*)Prefab_GetObjectName(state->Asset, writes[Index].Object), (Result.Message != NULL) ? (const char*)Result.Message : "");
            Error_Deconstruct(&Result);
        }
    }
    PropertyValue Value = PropertyValue_FromDataValue(&Row);
    Error Result = Properties_Set(writes[0].Table, self->_scratch, RowDescriptor->Key, &Value);
    if (Result.Code != ErrorCode_Success)
    {
        WorldPrefabsInternal_Warn(self, state, "writes what a row refuses, in the object \"%s\": %s",
            (const char*)Prefab_GetObjectName(state->Asset, writes[0].Object), (Result.Message != NULL) ? (const char*)Result.Message : "");
        Error_Deconstruct(&Result);
    }
    DataValue_Deconstruct(&Row);
}

static bool IsNestedWrite(const PrefabWrite* write)
{
    return (write->Kind == PrefabWriteKind_NestedProperty) || (write->Kind == PrefabWriteKind_NestedOverride);
}


// Static functions: applying.
/* One row of one instance: the template's value, the writes in order, and what differs from the real row written. */
static void ApplyRow(WorldPrefabs* self, PrefabCopyState* state, const PrefabWrite* writes, size_t count, const void* template, void* real)
{
    const PropertyTable* Table = writes[0].Table;
    const PropertyDescriptor* Row = writes[0].Row;
    PropertyValue Base;
    Error Result = Properties_Get(Table, template, Row->Key, &Base);
    if (Result.Code == ErrorCode_Success)
    {
        Result = Properties_Set(Table, self->_scratch, Row->Key, &Base);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return;
    }
    JSONObjectPool* Pool = WorldPrefabsInternal_GetPool(self);
    size_t Index = 0U;
    while (Index < count)
    {
        const PrefabWrite* Write = &writes[Index];
        if (IsNestedWrite(Write))
        {
            size_t RunEnd = Index + 1U;
            while ((RunEnd < count) && IsNestedWrite(&writes[RunEnd]))
            {
                RunEnd++;
            }
            ApplyNestedWrites(self, state, Write, RunEnd - Index);
            Index = RunEnd;
            continue;
        }
        Index++;
        if (Write->Kind != PrefabWriteKind_Row)
        {
            continue; // a restore: the template's value is there already
        }
        Result = PrefabInternal_ApplyRowValue(Table, self->_scratch, Row, Write->Axis, Write->Value, Pool);
        if (Result.Code != ErrorCode_Success)
        {
            WorldPrefabsInternal_Warn(self, state, "writes what a row refuses, in the object \"%s\": %s",
                (const char*)Prefab_GetObjectName(state->Asset, Write->Object), (Result.Message != NULL) ? (const char*)Result.Message : "");
            Error_Deconstruct(&Result);
        }
    }
    PropertyValue Desired;
    PropertyValue Current;
    Result = Properties_Get(Table, self->_scratch, Row->Key, &Desired);
    if (Result.Code == ErrorCode_Success)
    {
        Result = Properties_Get(Table, real, Row->Key, &Current);
    }
    if ((Result.Code == ErrorCode_Success) && !PropertyValue_Equals(&Desired, &Current))
    {
        Result = Properties_Set(Table, real, Row->Key, &Desired);
        self->_stats.WriteCount++;
    }
    if (Result.Code != ErrorCode_Success)
    {
        WorldPrefabsInternal_Warn(self, state, "cannot write a row of the object \"%s\": %s",
            (const char*)Prefab_GetObjectName(state->Asset, writes[0].Object), (Result.Message != NULL) ? (const char*)Result.Message : "");
        Error_Deconstruct(&Result);
    }
}

/* A scratch instance of a table: zeroed, large enough. */
static void PrepareScratch(WorldPrefabs* self, size_t size)
{
    if (size > self->_scratchSize)
    {
        Memory_Free(self->_scratch);
        self->_scratch = Memory_Allocate(size);
        self->_scratchSize = size;
    }
    Memory_Zero(self->_scratch, size);
}

/* The rows of one instance. */
static void ApplyInstance(WorldPrefabs* self, PrefabCopyState* state, const PrefabWrite* writes, size_t count)
{
    uint32_t ObjectIndex = writes[0].Object;
    int32_t Component = writes[0].Component;
    WorldObject* Object = WorldPrefabsInternal_FindHeld(self, state->ObjectIds[ObjectIndex]);
    if (Object == NULL)
    {
        return; // removed: what is written to it goes with it
    }
    const WorldObject* TemplateObject = Prefab_GetObjectTemplate(state->Asset, ObjectIndex);
    const void* Template = (Component == PREFAB_NO_COMPONENT) ? (const void*)TemplateObject
        : (const void*)WorldObject_GetComponent(TemplateObject, (WorldComponentType)Component);
    void* Real = (Component == PREFAB_NO_COMPONENT) ? (void*)Object : (void*)WorldObject_GetComponent(Object, (WorldComponentType)Component);
    if ((Template == NULL) || (Real == NULL))
    {
        return;
    }
    const PropertyTable* Table = writes[0].Table;
    PrepareScratch(self, Table->StructSize);
    size_t Start = 0U;
    while (Start < count)
    {
        size_t End = Start + 1U;
        while ((End < count) && (writes[End].Row == writes[Start].Row))
        {
            End++;
        }
        ApplyRow(self, state, &writes[Start], End - Start, Template, Real);
        Start = End;
    }
    Properties_Deconstruct(Table, self->_scratch);
}


// Functions.
void WorldPrefabsApply_ConstructScratch(WorldPrefabs* self)
{
    GenericBuffer_AllocateVariable(&self->_writes, 0U, sizeof(PrefabWrite));
}

void WorldPrefabsApply_DeconstructScratch(WorldPrefabs* self)
{
    Memory_Free(self->_writes._data);
    Memory_Free(self->_scratch);
}

void WorldPrefabsApply_FindRemoved(WorldPrefabs* self, PrefabCopyState* state, const WorldPrefabComponent* component, bool* outRemoved)
{
    const DataValue* Overrides = component->_objectOverrides;
    for (size_t Index = 0U; Index < DataValue_GetCompoundCount(Overrides); Index++)
    {
        const unsigned char* Path = DataValue_GetCompoundEntryName(Overrides, Index);
        const DataValue* Block = DataValue_GetCompoundEntryValue(Overrides, Index);
        size_t Length = 0U;
        while ((Path[Length] != 0U) && (Path[Length] != WORLD_PREFAB_PATH_SEPARATOR))
        {
            Length++;
        }
        uint32_t Object = 0U;
        if (!Prefab_TryFindObject(state->Asset, Path, Length, &Object))
        {
            WorldPrefabsInternal_Warn(self, state, "overrides an object its prefab no longer has: \"%s\".", (const char*)Path);
            continue;
        }
        if (Block->Type != DataType_Compound)
        {
            WorldPrefabsInternal_Warn(self, state, "has an override that is no object: \"%s\".", (const char*)Path);
            continue;
        }
        const DataValue* Flag = DataValue_FindCompoundEntry(Block, WORLD_PREFAB_OVERRIDE_IS_REMOVED_KEY);
        if (Flag == NULL)
        {
            continue;
        }
        if (Flag->Type != DataType_Bool)
        {
            WorldPrefabsInternal_Warn(self, state, "has an is_removed that is neither true nor false: \"%s\".", (const char*)Path);
            continue;
        }
        if (Path[Length] == 0U)
        {
            outRemoved[Object] = Flag->BoolValue;
        }
    }
}

bool WorldPrefabsApply_Run(WorldPrefabs* self, PrefabCopyState* state, const WorldPrefabComponent* component)
{
    GenericBuffer_Clear(&self->_writes);
    self->_writeBytes = 0U;
    AddRestores(self, state);
    size_t RestoreCount = self->_writes._count;
    AddPropertyWrites(self, state, component);
    AddOverrideWrites(self, state, component);
    size_t Work = self->_writes._count + (self->_writeBytes / WORLD_PREFABS_BYTES_PER_WORK);
    if (self->_hasWorked && (Work > self->_workLeft))
    {
        return false;
    }
    WorldPrefabsInternal_SpendWork(self, Work);
    size_t OtherBytes = (size_t)self->_stats.ByteCount - state->WrittenBytes;
    if ((OtherBytes + self->_writeBytes) > self->_limits.MaxBytes)
    {
        WorldPrefabsInternal_Warn(self, state, "writes more than the world's copies may hold: its values and overrides are left out.");
        GenericBuffer_SetCount(&self->_writes, RestoreCount);
        self->_writeBytes = 0U;
        state->IsShort = true;
    }
    state->WrittenBytes = self->_writeBytes;
    self->_stats.ByteCount = OtherBytes + self->_writeBytes;
    size_t Count = self->_writes._count;
    self->_stats.ApplyCount++;
    if (Count > 1U)
    {
        PrefabWrite Scratch[2];
        GenericBuffer_SortAscending(&self->_writes, CompareWrites, NULL, Scratch);
    }
    const PrefabWrite* Writes = (const PrefabWrite*)self->_writes._data;
    GenericBuffer_Clear(&state->Touched);
    size_t Start = 0U;
    while (Start < Count)
    {
        size_t End = Start + 1U;
        while ((End < Count) && (Writes[End].Object == Writes[Start].Object) && (Writes[End].Component == Writes[Start].Component))
        {
            End++;
        }
        ApplyInstance(self, state, &Writes[Start], End - Start);
        // Restores sort first, so a row whose last write is one was only restored: it is the template's again, and left alone from here on.
        for (size_t Index = Start; Index < End; Index++)
        {
            bool IsLastOfRow = ((Index + 1U) == End) || (Writes[Index + 1U].Row != Writes[Index].Row);
            if (IsLastOfRow && (Writes[Index].Kind != PrefabWriteKind_Restore))
            {
                PrefabTouchedRow Touched = { .Object = Writes[Index].Object, .Component = Writes[Index].Component,
                    .Table = Writes[Index].Table, .Row = Writes[Index].Row };
                GenericBuffer_AddLast(&state->Touched, &Touched);
            }
        }
        Start = End;
    }
    return true;
}
