#include "UIWidgetFactory.h"
#include "UIInternal.h"
#include "UIWidget.h"
#include "UIScreen.h"
#include <inttypes.h>
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
    const unsigned char* Name;
    size_t StructSize;
    const WidgetVTable* VTable;
    WidgetConstructor Constructor;
    bool IsFocusable;
    GenericBuffer CapabilityEntries; // of WidgetCapabilityEntry
    GenericBuffer Properties;        // of WidgetPropertyDescriptor
    ObjectPool Pool;                 // element size == StructSize
    size_t LiveCount;
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

/* Returns the record of a type id, or NULL if the id is out of range. The caller checks IsRegistered. */
static TypeRecord* GetTypeRecord(const UIWidgetFactory* self, uint64_t typeId)
{
    if ((typeId == WIDGET_TYPE_ID_INVALID) || (typeId > self->_types._count))
    {
        return NULL;
    }
    return GenericBuffer_GetPointerToElement((GenericBuffer*)&self->_types, (size_t)(typeId - 1U));
}

/* Returns the record of a registered type, or NULL. */
static TypeRecord* GetRegisteredTypeRecord(const UIWidgetFactory* self, uint64_t typeId)
{
    TypeRecord* Record = GetTypeRecord(self, typeId);
    return ((Record != NULL) && Record->IsRegistered) ? Record : NULL;
}

static void DisposeTypeRecord(TypeRecord* record, Error* firstError)
{
    Error PoolResult = ObjectPool_Deconstruct(&record->Pool);
    KeepFirstError(firstError, &PoolResult);
    Memory_Free(record->CapabilityEntries._data);
    Memory_Free(record->Properties._data);
    Memory_Zero(&record->Pool, sizeof(record->Pool));
    Memory_Zero(&record->CapabilityEntries, sizeof(record->CapabilityEntries));
    Memory_Zero(&record->Properties, sizeof(record->Properties));
}

static Error ValidateCapabilities(const UIWidgetFactory* self, const WidgetTypeDescriptor* descriptor)
{
    if ((descriptor->CapabilityCount > 0U) && (descriptor->Capabilities == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "UIWidgetFactory_RegisterType: Capabilities must not be NULL when CapabilityCount > 0.");
    }
    for (size_t Index = 0; Index < descriptor->CapabilityCount; Index++)
    {
        const WidgetCapabilityEntry* Entry = &descriptor->Capabilities[Index];
        if (Entry->Resolver == NULL)
        {
            return Error_Construct2(ErrorCode_IllegalArgument, "UIWidgetFactory_RegisterType: a capability entry has a NULL resolver.");
        }
        if ((Entry->CapabilityId == WIDGET_CAPABILITY_ID_INVALID) || (Entry->CapabilityId > self->_capabilities._count))
        {
            return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIWidgetFactory_RegisterType: a capability entry references an unknown capability.");
        }
        const CapabilityRecord* Capability = GenericBuffer_GetPointerToElement((GenericBuffer*)&self->_capabilities,
            (size_t)(Entry->CapabilityId - 1U));
        if (!Capability->IsRegistered)
        {
            return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIWidgetFactory_RegisterType: a capability entry references an unregistered capability.");
        }
    }
    return Error_CreateSuccess();
}

static Error ValidateProperties(const WidgetTypeDescriptor* descriptor)
{
    if ((descriptor->PropertyCount > 0U) && (descriptor->Properties == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "UIWidgetFactory_RegisterType: Properties must not be NULL when PropertyCount > 0.");
    }
    for (size_t Index = 0; Index < descriptor->PropertyCount; Index++)
    {
        const WidgetPropertyDescriptor* Property = &descriptor->Properties[Index];
        if ((Property->Id < 0) || (Property->Id >= WIDGET_BASE_PROPERTY_START))
        {
            return Error_Construct4(ErrorCode_ArgumentOutOfRange, "UIWidgetFactory_RegisterType: property id %" PRId32
                " is outside [0, WIDGET_BASE_PROPERTY_START).", Property->Id);
        }
        if (!UIPropertyType_IsValid(Property->Type))
        {
            return Error_Construct4(ErrorCode_IllegalArgument, "UIWidgetFactory_RegisterType: property %" PRId32
                " has an unknown type.", Property->Id);
        }
        for (size_t Other = 0; Other < Index; Other++)
        {
            if (descriptor->Properties[Other].Id == Property->Id)
            {
                return Error_Construct4(ErrorCode_IllegalArgument, "UIWidgetFactory_RegisterType: property id %" PRId32
                    " is declared twice.", Property->Id);
            }
        }
    }
    return Error_CreateSuccess();
}


// Internal functions (declared in UIInternal.h).
Error UIWidgetFactory_InternalReturnWidget(UIWidgetFactory* self, Widget* widget)
{
    TypeRecord* Record = GetRegisteredTypeRecord(self, widget->_typeId);
    if (Record == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWidgetFactory: a widget's type is no longer registered.");
    }
    if (Record->LiveCount > 0U)
    {
        Record->LiveCount--;
    }
    return ObjectPool_DisposeObject(&Record->Pool, widget);
}


// Public functions.
Error UIWidgetFactory_Construct(UIWidgetFactory* self)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIWidgetFactory_Construct: self must not be NULL.");
    }
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
    size_t LiveCount = UIWidgetFactory_GetTotalLiveWidgetCount(self);
    if (LiveCount > 0U)
    {
        FirstError = Error_Construct4(ErrorCode_InvalidOperation, "UIWidgetFactory_Deconstruct: %" PRIu64
            " widgets were still alive; deconstruct every screen before the factory.", (uint64_t)LiveCount);
    }
    for (size_t Index = 0; Index < self->_types._count; Index++)
    {
        TypeRecord* Record = GenericBuffer_GetPointerToElement(&self->_types, Index);
        if (Record->IsRegistered)
        {
            DisposeTypeRecord(Record, &FirstError);
        }
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
        return Error_Construct2(ErrorCode_BufferTooLarge, "UIWidgetFactory_RegisterCapability: capability registry could not grow.");
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
    if ((capabilityId == WIDGET_CAPABILITY_ID_INVALID) || (capabilityId > self->_capabilities._count))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIWidgetFactory_UnregisterCapability: capabilityId out of range.");
    }
    CapabilityRecord* Record = GenericBuffer_GetPointerToElement(&self->_capabilities, (size_t)(capabilityId - 1U));
    if (!Record->IsRegistered)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWidgetFactory_UnregisterCapability: capability is not registered.");
    }
    Record->IsRegistered = false;
    return Error_CreateSuccess();
}

Error UIWidgetFactory_RegisterType(UIWidgetFactory* self, const WidgetTypeDescriptor* descriptor, uint64_t* outTypeId)
{
    if ((self == NULL) || (descriptor == NULL) || (outTypeId == NULL) || (descriptor->VTable == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "UIWidgetFactory_RegisterType: self, descriptor, its VTable and outTypeId must not be NULL.");
    }
    if (descriptor->StructSize < sizeof(Widget))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIWidgetFactory_RegisterType: StructSize must hold a Widget.");
    }
    Error Result = ValidateCapabilities(self, descriptor);
    if (Result.Code == ErrorCode_Success)
    {
        Result = ValidateProperties(descriptor);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    TypeRecord Record;
    Memory_Zero(&Record, sizeof(Record));
    Record.IsRegistered = true;
    Record.Name = descriptor->Name;
    Record.StructSize = descriptor->StructSize;
    Record.VTable = descriptor->VTable;
    Record.Constructor = descriptor->Constructor;
    Record.IsFocusable = descriptor->IsFocusable;
    GenericBuffer_AllocateVariable(&Record.CapabilityEntries, descriptor->CapabilityCount, sizeof(WidgetCapabilityEntry));
    GenericBuffer_AllocateVariable(&Record.Properties, descriptor->PropertyCount, sizeof(WidgetPropertyDescriptor));

    bool IsCopied = true;
    for (size_t Index = 0; IsCopied && (Index < descriptor->CapabilityCount); Index++)
    {
        WidgetCapabilityEntry Entry = descriptor->Capabilities[Index];
        IsCopied = GenericBuffer_AddLast(&Record.CapabilityEntries, &Entry);
    }
    for (size_t Index = 0; IsCopied && (Index < descriptor->PropertyCount); Index++)
    {
        WidgetPropertyDescriptor Property = descriptor->Properties[Index];
        IsCopied = GenericBuffer_AddLast(&Record.Properties, &Property);
    }
    Result = IsCopied ? ObjectPool_Construct1(&Record.Pool, descriptor->StructSize, WIDGET_FACTORY_POOL_SECTION_CAPACITY)
        : Error_Construct2(ErrorCode_BufferTooLarge, "UIWidgetFactory_RegisterType: the type's tables could not be stored.");
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(Record.CapabilityEntries._data);
        Memory_Free(Record.Properties._data);
        return Result;
    }

    if (!GenericBuffer_AddLast(&self->_types, &Record))
    {
        Error Cleanup = Error_CreateSuccess();
        DisposeTypeRecord(&Record, &Cleanup);
        Error_Deconstruct(&Cleanup);
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
    if (Record->LiveCount > 0U)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWidgetFactory_UnregisterType: widgets of the type are alive.");
    }

    Error FirstError = Error_CreateSuccess();
    DisposeTypeRecord(Record, &FirstError);
    Record->IsRegistered = false;
    Record->Constructor = NULL;
    Record->VTable = NULL;
    Record->StructSize = 0;
    return FirstError;
}

Error UIWidgetFactory_CreateWidget(UIWidgetFactory* self, UIScreen* screen, uint64_t typeId, void* args, Widget** outWidget)
{
    if ((self == NULL) || (screen == NULL) || (outWidget == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIWidgetFactory_CreateWidget: self, screen and outWidget must not be NULL.");
    }
    *outWidget = NULL;
    if (UIScreen_GetFactory(screen) != self)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIWidgetFactory_CreateWidget: the screen uses another factory.");
    }
    TypeRecord* Record = GetTypeRecord(self, typeId);
    if (Record == NULL)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIWidgetFactory_CreateWidget: typeId out of range.");
    }
    if (!Record->IsRegistered)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIWidgetFactory_CreateWidget: type is not registered.");
    }

    void* Memory = NULL;
    Error Result = ObjectPool_GetNewObject(&Record->Pool, &Memory);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Widget* Created = Memory;
    WidgetConstructor Constructor = Record->Constructor;
    Widget_InternalInitializeBase(Created, Record->VTable, screen, typeId, Record->IsFocusable);
    // Counted from here, so the type cannot be unregistered (and its pool freed) while the constructor runs.
    Record->LiveCount++;

    // The id is reserved before the constructor runs and only resolves once it succeeded, so nothing can fail after
    // the constructor and a failing constructor leaves nothing registered.
    Result = UIScreen_InternalReserveWidget(screen, Created);
    if ((Result.Code == ErrorCode_Success) && (Constructor != NULL))
    {
        Result = Constructor(Created, args);
        if (Result.Code != ErrorCode_Success)
        {
            UIScreen_InternalCancelWidget(screen, Created->_id);
        }
    }
    // Fetched again: a constructor that registers a type may move the registry (records are never removed, so the
    // type's record is still there).
    Record = GetTypeRecord(self, typeId);
    if (Result.Code != ErrorCode_Success)
    {
        Record->LiveCount--;
        Widget_InternalReleaseBase(Created);
        Error DisposeResult = ObjectPool_DisposeObject(&Record->Pool, Created);
        Error_Deconstruct(&DisposeResult);
        return Result;
    }

    UIScreen_InternalActivateWidget(screen, Created->_id);
    *outWidget = Created;
    return Error_CreateSuccess();
}

size_t UIWidgetFactory_GetLiveWidgetCount(const UIWidgetFactory* self, uint64_t typeId)
{
    const TypeRecord* Record = GetRegisteredTypeRecord(self, typeId);
    return (Record != NULL) ? Record->LiveCount : 0U;
}

size_t UIWidgetFactory_GetTotalLiveWidgetCount(const UIWidgetFactory* self)
{
    size_t Total = 0;
    for (size_t Index = 0; Index < self->_types._count; Index++)
    {
        const TypeRecord* Record = GenericBuffer_GetPointerToElement((GenericBuffer*)&self->_types, Index);
        if (Record->IsRegistered)
        {
            Total += Record->LiveCount;
        }
    }
    return Total;
}

const unsigned char* UIWidgetFactory_GetTypeName(const UIWidgetFactory* self, uint64_t typeId)
{
    const TypeRecord* Record = GetRegisteredTypeRecord(self, typeId);
    return (Record != NULL) ? Record->Name : NULL;
}

bool UIWidgetFactory_TryGetPropertyDescriptor(const UIWidgetFactory* self, uint64_t typeId, int32_t propertyId,
    WidgetPropertyDescriptor* outDescriptor)
{
    const TypeRecord* Record = GetRegisteredTypeRecord(self, typeId);
    if (Record == NULL)
    {
        return false;
    }
    for (size_t Index = 0; Index < Record->Properties._count; Index++)
    {
        const WidgetPropertyDescriptor* Property = GenericBuffer_GetPointerToElement((GenericBuffer*)&Record->Properties, Index);
        if (Property->Id == propertyId)
        {
            *outDescriptor = *Property;
            return true;
        }
    }
    return false;
}

bool UIWidgetFactory_IsCapabilitySupported(const UIWidgetFactory* self, uint64_t typeId, uint64_t capabilityId)
{
    if (self == NULL)
    {
        return false;
    }
    const TypeRecord* Record = GetRegisteredTypeRecord(self, typeId);
    if (Record == NULL)
    {
        return false;
    }
    for (size_t Index = 0; Index < Record->CapabilityEntries._count; Index++)
    {
        const WidgetCapabilityEntry* Entry = GenericBuffer_GetPointerToElement((GenericBuffer*)&Record->CapabilityEntries, Index);
        if (Entry->CapabilityId == capabilityId)
        {
            return true;
        }
    }
    return false;
}

Error UIWidgetFactory_GetSupportedCapabilities(const UIWidgetFactory* self, uint64_t typeId, GenericBuffer* outCapabilityIds)
{
    if ((self == NULL) || (outCapabilityIds == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIWidgetFactory_GetSupportedCapabilities: self and outCapabilityIds must not be NULL.");
    }
    if (outCapabilityIds->_elementSize != sizeof(uint64_t))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIWidgetFactory_GetSupportedCapabilities: outCapabilityIds must be a uint64_t buffer.");
    }
    const TypeRecord* Record = GetTypeRecord(self, typeId);
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
        const WidgetCapabilityEntry* Entry = GenericBuffer_GetPointerToElement((GenericBuffer*)&Record->CapabilityEntries, Index);
        uint64_t Id = Entry->CapabilityId;
        if (!GenericBuffer_AddLast(outCapabilityIds, &Id))
        {
            return Error_Construct2(ErrorCode_BufferTooLarge, "UIWidgetFactory_GetSupportedCapabilities: output buffer could not grow.");
        }
    }
    return Error_CreateSuccess();
}

void* UIWidgetFactory_ResolveCapability(const UIWidgetFactory* self, uint64_t typeId, uint64_t capabilityId, void* widget)
{
    if ((self == NULL) || (widget == NULL))
    {
        return NULL;
    }
    const TypeRecord* Record = GetRegisteredTypeRecord(self, typeId);
    if (Record == NULL)
    {
        return NULL;
    }
    for (size_t Index = 0; Index < Record->CapabilityEntries._count; Index++)
    {
        const WidgetCapabilityEntry* Entry = GenericBuffer_GetPointerToElement((GenericBuffer*)&Record->CapabilityEntries, Index);
        if (Entry->CapabilityId == capabilityId)
        {
            return Entry->Resolver(widget);
        }
    }
    return NULL;
}
