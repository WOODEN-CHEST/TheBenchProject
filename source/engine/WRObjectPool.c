#include "WRObjectPool.h"


// Macros.
#define OBJECT_POOL_BITS_PER_OCCUPANCY_BYTE 8U


// Types.
typedef struct ObjectPoolSectionStruct
{
    unsigned char* Objects;
    unsigned char* OccupancyBits;
    size_t* FreeSlotStack;
    size_t InitializedCount;
    size_t OccupiedCount;
    size_t FreeSlotCount;
} ObjectPoolSection;


// Static functions.
static Error CreateNullArgumentError(const unsigned char* argumentName)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Object pool argument \"%s\" must not be null.",
        argumentName);
}

static Error CreateArgumentOutOfRangeError(const unsigned char* argumentName, const unsigned char* message)
{
    return Error_Construct3(ErrorCode_ArgumentOutOfRange,
        u8"Object pool argument \"%s\" is out of range: %s.",
        argumentName,
        message);
}

static Error CreateInvalidObjectError(void)
{
    return Error_Construct1(ErrorCode_IllegalArgument,
        u8"The provided object pointer does not belong to this object pool.");
}

static Error CreatePoolTooLargeError(void)
{
    return Error_Construct1(ErrorCode_BufferTooLarge,
        u8"The object pool cannot grow further because it would exceed addressable capacity.");
}

static Error ValidatePool(ObjectPool* self)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    return Error_CreateSuccess();
}

static Error ValidatePoolAndOutput(ObjectPool* self, void** outObject)
{
    Error Result = ValidatePool(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (outObject == NULL)
    {
        return CreateNullArgumentError(u8"outObject");
    }

    return Error_CreateSuccess();
}

static bool TryMultiplySizes(size_t left, size_t right, size_t* outProduct)
{
    if (outProduct == NULL)
    {
        return false;
    }
    if ((left > 0) && (right > (SIZE_MAX / left)))
    {
        return false;
    }

    *outProduct = left * right;
    return true;
}

static bool TryAddSizes(size_t left, size_t right, size_t* outSum)
{
    if (outSum == NULL)
    {
        return false;
    }
    if (left > (SIZE_MAX - right))
    {
        return false;
    }

    *outSum = left + right;
    return true;
}

static size_t GetSectionCount(ObjectPool* self)
{
    return self->_sections._count;
}

static ObjectPoolSection* GetSection(ObjectPool* self, size_t index)
{
    return GenericBuffer_GetPointerToElement(&self->_sections, index);
}

static size_t GetOccupancyByteCount(size_t sectionCapacity)
{
    size_t OccupancyByteCount = sectionCapacity / OBJECT_POOL_BITS_PER_OCCUPANCY_BYTE;

    if ((sectionCapacity % OBJECT_POOL_BITS_PER_OCCUPANCY_BYTE) != 0U)
    {
        OccupancyByteCount++;
    }

    return OccupancyByteCount;
}

static unsigned char GetOccupancyMask(size_t slotIndex)
{
    return (unsigned char)(1U << (unsigned int)(slotIndex % OBJECT_POOL_BITS_PER_OCCUPANCY_BYTE));
}

static bool IsSlotOccupied(const ObjectPoolSection* section, size_t slotIndex)
{
    size_t ByteIndex = slotIndex / OBJECT_POOL_BITS_PER_OCCUPANCY_BYTE;

    return (section->OccupancyBits[ByteIndex] & GetOccupancyMask(slotIndex)) != 0U;
}

static void SetSlotOccupied(ObjectPoolSection* section, size_t slotIndex, bool isOccupied)
{
    size_t ByteIndex = slotIndex / OBJECT_POOL_BITS_PER_OCCUPANCY_BYTE;
    unsigned char Mask = GetOccupancyMask(slotIndex);

    if (isOccupied)
    {
        section->OccupancyBits[ByteIndex] |= Mask;
        return;
    }

    section->OccupancyBits[ByteIndex] &= (unsigned char)(~Mask);
}

static bool SectionHasAvailableObject(const ObjectPoolSection* section, size_t sectionCapacity)
{
    return ((section->FreeSlotCount > 0) || (section->InitializedCount < sectionCapacity));
}

static unsigned char* GetObjectAddress(ObjectPool* self, ObjectPoolSection* section, size_t slotIndex)
{
    return section->Objects + (slotIndex * self->_elementSize);
}

static Error ResetObjectForReuse(ObjectPool* self, void* object)
{
    if (self->_lifecycle.ResetObject != NULL)
    {
        return self->_lifecycle.ResetObject(object, self->_userData);
    }

    Memory_Zero(object, self->_elementSize);
    return Error_CreateSuccess();
}

static Error ConstructObjectIfNeeded(ObjectPool* self, void* object)
{
    if (self->_lifecycle.ConstructObject != NULL)
    {
        return self->_lifecycle.ConstructObject(object, self->_userData);
    }

    return Error_CreateSuccess();
}

static Error DeconstructObjectIfNeeded(ObjectPool* self, void* object)
{
    if (self->_lifecycle.DeconstructObject != NULL)
    {
        return self->_lifecycle.DeconstructObject(object, self->_userData);
    }

    return Error_CreateSuccess();
}

static Error ResetSectionForReuse(ObjectPool* self, ObjectPoolSection* section)
{
    size_t OccupancyByteCount = GetOccupancyByteCount(self->_sectionCapacity);

    for (size_t SlotIndex = 0; SlotIndex < section->InitializedCount; SlotIndex++)
    {
        if (!IsSlotOccupied(section, SlotIndex))
        {
            continue;
        }

        Error Result = ResetObjectForReuse(self, GetObjectAddress(self, section, SlotIndex));
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    Memory_Zero(section->OccupancyBits, OccupancyByteCount);
    section->OccupiedCount = 0;
    section->FreeSlotCount = section->InitializedCount;

    for (size_t SlotIndex = 0; SlotIndex < section->InitializedCount; SlotIndex++)
    {
        section->FreeSlotStack[SlotIndex] = SlotIndex;
    }

    return Error_CreateSuccess();
}

static void DeconstructSection(ObjectPoolSection* section)
{
    if (section == NULL)
    {
        return;
    }

    Memory_Free(section->Objects);
    Memory_Free(section->OccupancyBits);
    Memory_Free(section->FreeSlotStack);
    Memory_Zero(section, sizeof(*section));
}

static Error ConstructSection(ObjectPool* self, ObjectPoolSection* outSection)
{
    size_t ObjectBytes = 0;
    size_t OccupancyByteCount = 0;
    size_t FreeStackBytes = 0;

    if (outSection == NULL)
    {
        return CreateNullArgumentError(u8"outSection");
    }
    if (!TryMultiplySizes(self->_sectionCapacity, self->_elementSize, &ObjectBytes))
    {
        return CreatePoolTooLargeError();
    }

    OccupancyByteCount = GetOccupancyByteCount(self->_sectionCapacity);
    if (!TryMultiplySizes(self->_sectionCapacity, sizeof(size_t), &FreeStackBytes))
    {
        return CreatePoolTooLargeError();
    }

    Memory_Zero(outSection, sizeof(*outSection));
    outSection->Objects = Memory_Allocate(ObjectBytes);
    outSection->OccupancyBits = Memory_Allocate(OccupancyByteCount);
    outSection->FreeSlotStack = Memory_Allocate(FreeStackBytes);
    Memory_Zero(outSection->Objects, ObjectBytes);
    Memory_Zero(outSection->OccupancyBits, OccupancyByteCount);
    return Error_CreateSuccess();
}

static Error AppendSection(ObjectPool* self, ObjectPoolSection* section, size_t* outSectionIndex)
{
    if (outSectionIndex == NULL)
    {
        return CreateNullArgumentError(u8"outSectionIndex");
    }
    if (GetSectionCount(self) == SIZE_MAX)
    {
        return CreatePoolTooLargeError();
    }
    if (!GenericBuffer_AddLast(&self->_sections, section))
    {
        return CreatePoolTooLargeError();
    }

    *outSectionIndex = GetSectionCount(self) - 1;
    return Error_CreateSuccess();
}

static Error EnsureAvailableSection(ObjectPool* self, size_t* outSectionIndex)
{
    size_t SectionCount = GetSectionCount(self);

    if (outSectionIndex == NULL)
    {
        return CreateNullArgumentError(u8"outSectionIndex");
    }

    if (SectionCount > 0)
    {
        size_t StartIndex = self->_nextSectionSearchIndex % SectionCount;

        for (size_t Offset = 0; Offset < SectionCount; Offset++)
        {
            size_t SectionIndex = (StartIndex + Offset) % SectionCount;
            ObjectPoolSection* Section = GetSection(self, SectionIndex);

            if (SectionHasAvailableObject(Section, self->_sectionCapacity))
            {
                *outSectionIndex = SectionIndex;
                return Error_CreateSuccess();
            }
        }
    }

    ObjectPoolSection NewSection;
    Error Result = ConstructSection(self, &NewSection);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = AppendSection(self, &NewSection, outSectionIndex);
    if (Result.Code != ErrorCode_Success)
    {
        DeconstructSection(&NewSection);
        return Result;
    }

    return Error_CreateSuccess();
}

static Error TryFindSectionByObject(ObjectPool* self, void* object, size_t* outSectionIndex, size_t* outSlotIndex)
{
    uintptr_t TargetAddress = (uintptr_t)object;
    size_t SectionCount = GetSectionCount(self);

    if (outSectionIndex == NULL)
    {
        return CreateNullArgumentError(u8"outSectionIndex");
    }
    if (outSlotIndex == NULL)
    {
        return CreateNullArgumentError(u8"outSlotIndex");
    }

    for (size_t SectionIndex = 0; SectionIndex < SectionCount; SectionIndex++)
    {
        ObjectPoolSection* Section = GetSection(self, SectionIndex);
        uintptr_t SectionStart = (uintptr_t)Section->Objects;
        size_t SectionObjectBytes = 0;

        if (!TryMultiplySizes(self->_sectionCapacity, self->_elementSize, &SectionObjectBytes))
        {
            return CreatePoolTooLargeError();
        }

        uintptr_t SectionEnd = 0;
        if (!TryAddSizes(SectionStart, SectionObjectBytes, &SectionEnd))
        {
            return CreatePoolTooLargeError();
        }
        if ((TargetAddress < SectionStart) || (TargetAddress >= SectionEnd))
        {
            continue;
        }

        size_t OffsetBytes = (size_t)(TargetAddress - SectionStart);
        if ((OffsetBytes % self->_elementSize) != 0U)
        {
            return CreateInvalidObjectError();
        }

        *outSectionIndex = SectionIndex;
        *outSlotIndex = OffsetBytes / self->_elementSize;
        return Error_CreateSuccess();
    }

    return CreateInvalidObjectError();
}


// Public functions.
Error ObjectPool_Construct2(ObjectPool* self,
    size_t elementSize,
    size_t sectionCapacity,
    ObjectPoolLifecycle lifecycle,
    void* userData)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (elementSize == 0)
    {
        return CreateArgumentOutOfRangeError(u8"elementSize", u8"must be greater than zero");
    }
    if (sectionCapacity == 0)
    {
        return CreateArgumentOutOfRangeError(u8"sectionCapacity", u8"must be greater than zero");
    }

    Memory_Zero(self, sizeof(*self));
    GenericBuffer_AllocateVariable(&self->_sections, 0, sizeof(ObjectPoolSection));
    self->_elementSize = elementSize;
    self->_sectionCapacity = sectionCapacity;
    self->_nextSectionSearchIndex = 0;
    self->_lifecycle = lifecycle;
    self->_userData = userData;
    return Error_CreateSuccess();
}

Error ObjectPool_Construct1(ObjectPool* self, size_t elementSize, size_t sectionCapacity)
{
    return ObjectPool_Construct2(self,
        elementSize,
        sectionCapacity,
        (ObjectPoolLifecycle){ 0 },
        NULL);
}

Error ObjectPool_Deconstruct(ObjectPool* self)
{
    Error FirstError = Error_CreateSuccess();
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return Error_CreateSuccess();
    }

    for (size_t SectionIndex = 0; SectionIndex < GetSectionCount(self); SectionIndex++)
    {
        ObjectPoolSection* Section = GetSection(self, SectionIndex);
        for (size_t SlotIndex = 0; SlotIndex < Section->InitializedCount; SlotIndex++)
        {
            Result = DeconstructObjectIfNeeded(self, GetObjectAddress(self, Section, SlotIndex));
            if ((Result.Code != ErrorCode_Success) && (FirstError.Code == ErrorCode_Success))
            {
                FirstError = Result;
            }
        }
        DeconstructSection(Section);
    }

    Memory_Free(self->_sections._data);
    Memory_Zero(self, sizeof(*self));
    return FirstError;
}

Error ObjectPool_GetNewObject(ObjectPool* self, void** outObject)
{
    size_t SectionIndex = 0;
    size_t SlotIndex = 0;
    ObjectPoolSection* Section = NULL;
    Error Result = ValidatePoolAndOutput(self, outObject);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    *outObject = NULL;
    Result = EnsureAvailableSection(self, &SectionIndex);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Section = GetSection(self, SectionIndex);
    if (Section->FreeSlotCount > 0)
    {
        Section->FreeSlotCount--;
        SlotIndex = Section->FreeSlotStack[Section->FreeSlotCount];
    }
    else
    {
        SlotIndex = Section->InitializedCount;
        Result = ConstructObjectIfNeeded(self, GetObjectAddress(self, Section, SlotIndex));
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        Section->InitializedCount++;
    }

    SetSlotOccupied(Section, SlotIndex, true);
    Section->OccupiedCount++;
    self->_nextSectionSearchIndex = SectionIndex;
    *outObject = GetObjectAddress(self, Section, SlotIndex);
    return Error_CreateSuccess();
}

Error ObjectPool_DisposeObject(ObjectPool* self, void* object)
{
    size_t SectionIndex = 0;
    size_t SlotIndex = 0;
    ObjectPoolSection* Section = NULL;
    unsigned char* ObjectBytes = object;
    Error Result = ValidatePool(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (object == NULL)
    {
        return CreateNullArgumentError(u8"object");
    }

    Result = TryFindSectionByObject(self, object, &SectionIndex, &SlotIndex);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Section = GetSection(self, SectionIndex);
    if (!IsSlotOccupied(Section, SlotIndex))
    {
        return Error_CreateSuccess();
    }

    SetSlotOccupied(Section, SlotIndex, false);
    Result = ResetObjectForReuse(self, ObjectBytes);
    if (Result.Code != ErrorCode_Success)
    {
        SetSlotOccupied(Section, SlotIndex, true);
        return Result;
    }
    Section->FreeSlotStack[Section->FreeSlotCount] = SlotIndex;
    Section->FreeSlotCount++;
    Section->OccupiedCount--;
    self->_nextSectionSearchIndex = SectionIndex;
    return Error_CreateSuccess();
}

Error ObjectPool_Clear(ObjectPool* self)
{
    Error Result = ValidatePool(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    for (size_t SectionIndex = 0; SectionIndex < GetSectionCount(self); SectionIndex++)
    {
        Result = ResetSectionForReuse(self, GetSection(self, SectionIndex));
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    self->_nextSectionSearchIndex = 0;
    return Error_CreateSuccess();
}
