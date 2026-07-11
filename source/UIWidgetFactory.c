#include "UIWidgetFactory.h"
#include "wr/WRObjectPool.h"
#include "wr/WRMemory.h"


// Macros.
/** Objects per pool section for each widget type's instance pool (its growth granularity). */
#define WIDGET_FACTORY_POOL_SECTION_CAPACITY ((size_t)16)


// Types.
/* One capability slot; the capability id is the slot index + 1. */
typedef struct CapabilityRecordStruct
{
    bool IsRegistered;
} CapabilityRecord;

/* One widget type slot; the type id is the slot index + 1. Holds the type's instance pool. */
typedef struct TypeRecordStruct
{
    bool IsRegistered;
    size_t StructSize;
    WidgetConstructor Constructor;
    GenericBuffer CapabilityEntries; // of WidgetCapabilityEntry
    ObjectPool Pool;                 // element size == StructSize
} TypeRecord;


// Static functions.
/* Keeps the first non-success error and releases any later one, for best-effort teardown loops. */
static void KeepFirstError(Error* first, Error* candidate)
{
    if ((candidate->Code != ErrorCode_Success) && (first->Code == ErrorCode_Success))
    {
        *first = *candidate;
    }
    else
    {
        Error_Deconstruct(candidate);
    }
}

/* Returns the type record for a type id, or NULL if the id is out of range. The caller checks IsRegistered. */
static TypeRecord* GetTypeRecord(UIWidgetFactory* self, uint64_t typeId)
{
    if ((typeId == 0) || (typeId > self->_types._count))
    {
        return NULL;
    }
    return GenericBuffer_GetPointerToElement(&self->_types, (size_t)(typeId - 1));
}

/* Frees the transient allocations of a half-built type record (used on registration failure). */
static void DisposePartialTypeRecord(TypeRecord* record)
{
    Error PoolResult = ObjectPool_Deconstruct(&record->Pool);
    Error_Deconstruct(&PoolResult);
    Memory_Free(record->CapabilityEntries._data);
}


// Public functions.
Error UIWidgetFactory_Construct(UIWidgetFactory* self, UIScreen* screen)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIWidgetFactory_Construct: self must not be NULL.");
    }

    self->_screen = screen;
    GenericBuffer_AllocateVariable(&self->_capabilities, 8U, sizeof(CapabilityRecord));
    GenericBuffer_AllocateVariable(&self->_types, 8U, sizeof(TypeRecord));
    return Error_CreateSuccess();
}

Error UIWidgetFactory_Deconstruct(UIWidgetFactory* self)
{
    if (self == NULL)
    {
        return Error_CreateSuccess();
    }

    Error FirstError = Error_CreateSuccess();
    for (size_t Index = 0; Index < self->_types._count; Index++)
    {
        TypeRecord* Record = GenericBuffer_GetPointerToElement(&self->_types, Index);
        if (!Record->IsRegistered)
        {
            continue;
        }
        Error PoolResult = ObjectPool_Deconstruct(&Record->Pool);
        KeepFirstError(&FirstError, &PoolResult);
        Memory_Free(Record->CapabilityEntries._data);
    }

    Memory_Free(self->_types._data);
    Memory_Free(self->_capabilities._data);
    Memory_Zero(self, sizeof(*self));
    return FirstError;
}

Error UIWidgetFactory_RegisterCapability(UIWidgetFactory* self, uint64_t* outCapabilityId)
{
    if ((self == NULL) || (outCapabilityId == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "UIWidgetFactory_RegisterCapability: self and outCapabilityId must not be NULL.");
    }

    CapabilityRecord Record = { .IsRegistered = true };
    if (!GenericBuffer_AddLast(&self->_capabilities, &Record))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge,
            "UIWidgetFactory_RegisterCapability: capability registry could not grow.");
    }

    *outCapabilityId = self->_capabilities._count; // id == index + 1 == count after the add
    return Error_CreateSuccess();
}

Error UIWidgetFactory_UnregisterCapability(UIWidgetFactory* self, uint64_t capabilityId)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIWidgetFactory_UnregisterCapability: self must not be NULL.");
    }
    if ((capabilityId == 0) || (capabilityId > self->_capabilities._count))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIWidgetFactory_UnregisterCapability: capabilityId out of range.");
    }

    CapabilityRecord* Record = GenericBuffer_GetPointerToElement(&self->_capabilities, (size_t)(capabilityId - 1));
    if (!Record->IsRegistered)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWidgetFactory_UnregisterCapability: capability is not registered.");
    }

    Record->IsRegistered = false;
    return Error_CreateSuccess();
}

Error UIWidgetFactory_RegisterType(UIWidgetFactory* self,
    size_t widgetStructSize,
    WidgetConstructor constructor,
    const WidgetCapabilityEntry* capabilities,
    size_t capabilityCount,
    uint64_t* outTypeId)
{
    if ((self == NULL) || (constructor == NULL) || (outTypeId == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "UIWidgetFactory_RegisterType: self, constructor and outTypeId must not be NULL.");
    }
    if (widgetStructSize == 0)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIWidgetFactory_RegisterType: widgetStructSize must be > 0.");
    }
    if ((capabilityCount > 0) && (capabilities == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "UIWidgetFactory_RegisterType: capabilities must not be NULL when capabilityCount > 0.");
    }

    // Validate every capability entry before allocating anything.
    for (size_t Index = 0; Index < capabilityCount; Index++)
    {
        if (capabilities[Index].Resolver == NULL)
        {
            return Error_Construct2(ErrorCode_IllegalArgument, "UIWidgetFactory_RegisterType: a capability entry has a NULL resolver.");
        }
        uint64_t CapabilityId = capabilities[Index].CapabilityId;
        if ((CapabilityId == 0) || (CapabilityId > self->_capabilities._count))
        {
            return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIWidgetFactory_RegisterType: a capability entry references an out-of-range capability.");
        }
        CapabilityRecord* CapabilityRec = GenericBuffer_GetPointerToElement(&self->_capabilities, (size_t)(CapabilityId - 1));
        if (!CapabilityRec->IsRegistered)
        {
            return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIWidgetFactory_RegisterType: a capability entry references an unregistered capability.");
        }
    }

    TypeRecord Record;
    Record.IsRegistered = true;
    Record.StructSize = widgetStructSize;
    Record.Constructor = constructor;
    GenericBuffer_AllocateVariable(&Record.CapabilityEntries, (capabilityCount > 0) ? capabilityCount : 1U, sizeof(WidgetCapabilityEntry));

    Error PoolResult = ObjectPool_Construct1(&Record.Pool, widgetStructSize, WIDGET_FACTORY_POOL_SECTION_CAPACITY);
    if (PoolResult.Code != ErrorCode_Success)
    {
        Memory_Free(Record.CapabilityEntries._data);
        return PoolResult;
    }

    for (size_t Index = 0; Index < capabilityCount; Index++)
    {
        WidgetCapabilityEntry Entry = capabilities[Index];
        if (!GenericBuffer_AddLast(&Record.CapabilityEntries, &Entry))
        {
            DisposePartialTypeRecord(&Record);
            return Error_Construct2(ErrorCode_BufferTooLarge, "UIWidgetFactory_RegisterType: capability entry buffer could not grow.");
        }
    }

    if (!GenericBuffer_AddLast(&self->_types, &Record))
    {
        DisposePartialTypeRecord(&Record);
        return Error_Construct2(ErrorCode_BufferTooLarge, "UIWidgetFactory_RegisterType: type registry could not grow.");
    }

    *outTypeId = self->_types._count; // id == index + 1 == count after the add
    return Error_CreateSuccess();
}

Error UIWidgetFactory_UnregisterType(UIWidgetFactory* self, uint64_t typeId)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIWidgetFactory_UnregisterType: self must not be NULL.");
    }
    TypeRecord* Record = GetTypeRecord(self, typeId);
    if (Record == NULL)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIWidgetFactory_UnregisterType: typeId out of range.");
    }
    if (!Record->IsRegistered)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWidgetFactory_UnregisterType: type is not registered.");
    }

    Error PoolResult = ObjectPool_Deconstruct(&Record->Pool);
    Memory_Free(Record->CapabilityEntries._data);
    Memory_Zero(&Record->Pool, sizeof(Record->Pool));
    Memory_Zero(&Record->CapabilityEntries, sizeof(Record->CapabilityEntries));
    Record->IsRegistered = false;
    Record->Constructor = NULL;
    Record->StructSize = 0;
    return PoolResult;
}

Error UIWidgetFactory_ConstructWidget(UIWidgetFactory* self, uint64_t typeId, void* args, Widget** outWidget)
{
    if ((self == NULL) || (outWidget == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIWidgetFactory_ConstructWidget: self and outWidget must not be NULL.");
    }
    *outWidget = NULL;

    TypeRecord* Record = GetTypeRecord(self, typeId);
    if (Record == NULL)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIWidgetFactory_ConstructWidget: typeId out of range.");
    }
    if (!Record->IsRegistered)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWidgetFactory_ConstructWidget: type is not registered.");
    }

    void* Memory = NULL;
    Error PoolResult = ObjectPool_GetNewObject(&Record->Pool, &Memory);
    if (PoolResult.Code != ErrorCode_Success)
    {
        return PoolResult;
    }

    Error ConstructResult = Record->Constructor(Memory, self->_screen, typeId, args);
    if (ConstructResult.Code != ErrorCode_Success)
    {
        // The constructor must not have grown the type registry, but re-fetch the record defensively.
        TypeRecord* CurrentRecord = GetTypeRecord(self, typeId);
        if (CurrentRecord != NULL)
        {
            Error DisposeResult = ObjectPool_DisposeObject(&CurrentRecord->Pool, Memory);
            Error_Deconstruct(&DisposeResult);
        }
        return ConstructResult;
    }

    *outWidget = Memory;
    return Error_CreateSuccess();
}

Error UIWidgetFactory_ReturnWidget(UIWidgetFactory* self, uint64_t typeId, void* widget)
{
    if ((self == NULL) || (widget == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIWidgetFactory_ReturnWidget: self and widget must not be NULL.");
    }
    TypeRecord* Record = GetTypeRecord(self, typeId);
    if (Record == NULL)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIWidgetFactory_ReturnWidget: typeId out of range.");
    }
    if (!Record->IsRegistered)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWidgetFactory_ReturnWidget: type is not registered.");
    }

    return ObjectPool_DisposeObject(&Record->Pool, widget);
}

bool UIWidgetFactory_IsCapabilitySupported(UIWidgetFactory* self, uint64_t typeId, uint64_t capabilityId)
{
    if (self == NULL)
    {
        return false;
    }
    TypeRecord* Record = GetTypeRecord(self, typeId);
    if ((Record == NULL) || !Record->IsRegistered)
    {
        return false;
    }

    for (size_t Index = 0; Index < Record->CapabilityEntries._count; Index++)
    {
        WidgetCapabilityEntry* Entry = GenericBuffer_GetPointerToElement(&Record->CapabilityEntries, Index);
        if (Entry->CapabilityId == capabilityId)
        {
            return true;
        }
    }
    return false;
}

Error UIWidgetFactory_GetSupportedCapabilities(UIWidgetFactory* self, uint64_t typeId, GenericBuffer* outCapabilityIds)
{
    if ((self == NULL) || (outCapabilityIds == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIWidgetFactory_GetSupportedCapabilities: self and outCapabilityIds must not be NULL.");
    }
    if (outCapabilityIds->_elementSize != sizeof(uint64_t))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIWidgetFactory_GetSupportedCapabilities: outCapabilityIds must be a uint64_t buffer.");
    }
    TypeRecord* Record = GetTypeRecord(self, typeId);
    if (Record == NULL)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIWidgetFactory_GetSupportedCapabilities: typeId out of range.");
    }
    if (!Record->IsRegistered)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWidgetFactory_GetSupportedCapabilities: type is not registered.");
    }

    for (size_t Index = 0; Index < Record->CapabilityEntries._count; Index++)
    {
        WidgetCapabilityEntry* Entry = GenericBuffer_GetPointerToElement(&Record->CapabilityEntries, Index);
        uint64_t Id = Entry->CapabilityId;
        if (!GenericBuffer_AddLast(outCapabilityIds, &Id))
        {
            return Error_Construct2(ErrorCode_BufferTooLarge, "UIWidgetFactory_GetSupportedCapabilities: output buffer could not grow.");
        }
    }
    return Error_CreateSuccess();
}

void* UIWidgetFactory_ResolveCapability(UIWidgetFactory* self, uint64_t typeId, uint64_t capabilityId, void* widget)
{
    if ((self == NULL) || (widget == NULL))
    {
        return NULL;
    }
    TypeRecord* Record = GetTypeRecord(self, typeId);
    if ((Record == NULL) || !Record->IsRegistered)
    {
        return NULL;
    }

    for (size_t Index = 0; Index < Record->CapabilityEntries._count; Index++)
    {
        WidgetCapabilityEntry* Entry = GenericBuffer_GetPointerToElement(&Record->CapabilityEntries, Index);
        if (Entry->CapabilityId == capabilityId)
        {
            return Entry->Resolver(widget);
        }
    }
    return NULL;
}
