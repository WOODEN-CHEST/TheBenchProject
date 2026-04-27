#include "WRArrayList.h"


// Types.
typedef struct ArrayListEnumeratorStruct
{
    CollectionEnumerator Base;
    ArrayList* _arrayList;
    size_t _currentIndex;
} ArrayListEnumerator;


// Static functions.
static size_t ArrayList_ListGetElementCount(void* self);

static Error ArrayList_ListInsert(void* self, size_t index, void* element);

static Error ArrayList_ListRemove(void* self, size_t index);

static Error ArrayList_ListReplace(void* self, size_t index, void* element);

static Error ArrayList_ListInsertRange(void* self, size_t index, void* elements, size_t elementCount);

static Error ArrayList_ListRemoveRange(void* self, size_t index, size_t elementCount);

static Error ArrayList_ListReplaceRange(void* self, size_t index, void* elements, size_t elementCount);

static Error ArrayList_ListClear(void* self);

static Error ArrayList_ListGetElement(void* self, size_t index, void* outElement);

static Error ArrayList_ListGetPointerToElement(void* self, size_t index, void** outElementPointer);

static void ArrayList_ListDeconstruct(void* self);

static CollectionEnumerator* ArrayList_ListGetEnumerator(void* self);

static Error CreateNullArgumentError(const unsigned char* argumentName)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Array list argument \"%s\" must not be null.",
        argumentName);
}

static Error CreateNullArgumentErrorForList(const unsigned char* argumentName)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"List argument \"%s\" must not be null.",
        argumentName);
}

static Error CreateReadOnlyError(void)
{
    return Error_Construct1(ErrorCode_InvalidOperation,
        u8"The list is read-only.");
}

static Error CreateIndexOutOfBoundsError(size_t index, size_t elementCount)
{
    return Error_Construct3(ErrorCode_IndexOutOfBounds,
        u8"Index %zu is outside the valid list range for %zu elements.",
        index,
        elementCount);
}

static Error CreateRangeOutOfBoundsError(size_t index, size_t elementCount, size_t listCount)
{
    return Error_Construct3(ErrorCode_ArgumentOutOfRange,
        u8"Range starting at %zu with %zu elements is outside the valid list range for %zu elements.",
        index,
        elementCount,
        listCount);
}

static Error CreateCapacityEnsureError(size_t requiredCapacity)
{
    return Error_Construct3(ErrorCode_InvalidOperation,
        u8"Could not ensure list capacity of %zu elements.",
        requiredCapacity);
}

static Error CreateEnumerationCompletedError(void)
{
    return Error_Construct1(ErrorCode_InvalidOperation,
        u8"The collection enumerator has no more elements.");
}

static void InitializeEmptyBuffer(GenericBuffer* buffer)
{
    if (buffer == NULL)
    {
        return;
    }

    buffer->_data = NULL;
    buffer->_capacity = 0;
    buffer->_count = 0;
    buffer->_elementSize = 0;
    buffer->_userData = NULL;
    buffer->_requestMoreSpaceCallback = NULL;
    buffer->_flags = GenericBufferFlags_None;
}

static bool ArrayList_RequestMoreSpace(GenericBuffer* destination, size_t requestedCapacity)
{
    unsigned char* ResizedData = NULL;
    size_t ByteCount = 0;

    if (destination == NULL)
    {
        return false;
    }
    if (destination->_elementSize == 0)
    {
        return false;
    }
    if ((requestedCapacity > 0) && ((SIZE_MAX / destination->_elementSize) < requestedCapacity))
    {
        return false;
    }

    ByteCount = requestedCapacity * destination->_elementSize;
    ResizedData = Memory_Reallocate(destination->_data, ByteCount);
    destination->_data = ResizedData;
    destination->_capacity = requestedCapacity;
    return true;
}

static void InitializeInterfaces(ArrayList* self)
{
    static const IListVTable ListTemplate =
    {
        .Self = NULL,
        ._getElementCount = ArrayList_ListGetElementCount,
        ._insert = ArrayList_ListInsert,
        ._remove = ArrayList_ListRemove,
        ._replace = ArrayList_ListReplace,
        ._insertRange = ArrayList_ListInsertRange,
        ._removeRange = ArrayList_ListRemoveRange,
        ._replaceRange = ArrayList_ListReplaceRange,
        ._clear = ArrayList_ListClear,
        ._getElement = ArrayList_ListGetElement,
        ._getPointerToElement = ArrayList_ListGetPointerToElement,
        ._deconstruct = ArrayList_ListDeconstruct,
    };
    static const ICollectionVtable CollectionTemplate =
    {
        .Self = NULL,
        ._getEnumerator = ArrayList_ListGetEnumerator,
    };
    IListFlags Flags = IListFlags_None;

    if ((self != NULL) && (self->_activeBuffer != NULL) && GenericBuffer_IsReadOnly(self->_activeBuffer))
    {
        Flags = IListFlags_IsReadOnly;
    }

    self->_list._elementSize = (self->_activeBuffer == NULL) ? 0 : self->_activeBuffer->_elementSize;
    self->_list._flags = Flags;
    self->_list._vtable = ListTemplate;
    self->_list._vtable.Self = self;
    self->_list._collection._vtable = CollectionTemplate;
    self->_list._collection._vtable.Self = self;
}

static Error ValidateArrayListAndElement(ArrayList* self, void* element)
{
    if (self == NULL)
    {
        return CreateNullArgumentErrorForList(u8"self");
    }
    if (element == NULL)
    {
        return CreateNullArgumentErrorForList(u8"element");
    }

    return Error_CreateSuccess();
}

static Error ValidateArrayListAndElements(ArrayList* self, void* elements, size_t elementCount)
{
    if (self == NULL)
    {
        return CreateNullArgumentErrorForList(u8"self");
    }
    if ((elementCount > 0) && (elements == NULL))
    {
        return CreateNullArgumentErrorForList(u8"elements");
    }

    return Error_CreateSuccess();
}

static Error ValidateWritable(ArrayList* self)
{
    if (self == NULL)
    {
        return CreateNullArgumentErrorForList(u8"self");
    }
    if ((self->_activeBuffer != NULL) && GenericBuffer_IsReadOnly(self->_activeBuffer))
    {
        return CreateReadOnlyError();
    }

    return Error_CreateSuccess();
}

static Error ValidateInsertIndex(ArrayList* self, size_t index)
{
    size_t ElementCount = 0;

    if (self == NULL)
    {
        return CreateNullArgumentErrorForList(u8"self");
    }

    ElementCount = self->_activeBuffer->_count;
    if (index > ElementCount)
    {
        return CreateIndexOutOfBoundsError(index, ElementCount);
    }

    return Error_CreateSuccess();
}

static Error ValidateExistingIndex(ArrayList* self, size_t index)
{
    size_t ElementCount = 0;

    if (self == NULL)
    {
        return CreateNullArgumentErrorForList(u8"self");
    }

    ElementCount = self->_activeBuffer->_count;
    if (index >= ElementCount)
    {
        return CreateIndexOutOfBoundsError(index, ElementCount);
    }

    return Error_CreateSuccess();
}

static Error ValidateExistingRange(ArrayList* self, size_t index, size_t elementCount)
{
    size_t ListCount = 0;

    if (self == NULL)
    {
        return CreateNullArgumentErrorForList(u8"self");
    }

    ListCount = self->_activeBuffer->_count;
    if (index > ListCount)
    {
        return CreateIndexOutOfBoundsError(index, ListCount);
    }
    if (elementCount > (ListCount - index))
    {
        return CreateRangeOutOfBoundsError(index, elementCount, ListCount);
    }

    return Error_CreateSuccess();
}

static Error ValidateEnumeratorAndOutput(ArrayListEnumerator* self, void* outValue)
{
    if (self == NULL)
    {
        return CreateNullArgumentErrorForList(u8"self");
    }
    if (outValue == NULL)
    {
        return CreateNullArgumentErrorForList(u8"outValue");
    }

    return Error_CreateSuccess();
}

static Error ValidateEnumeratorPointerOutput(ArrayListEnumerator* self, void** outPointer)
{
    if (self == NULL)
    {
        return CreateNullArgumentErrorForList(u8"self");
    }
    if (outPointer == NULL)
    {
        return CreateNullArgumentErrorForList(u8"outPointer");
    }

    return Error_CreateSuccess();
}

static size_t ArrayList_ListGetElementCount(void* self)
{
    ArrayList* ArrayListSelf = self;

    if ((ArrayListSelf == NULL) || (ArrayListSelf->_activeBuffer == NULL))
    {
        return 0;
    }

    return ArrayListSelf->_activeBuffer->_count;
}

static Error ArrayList_ListInsert(void* self, size_t index, void* element)
{
    ArrayList* ArrayListSelf = self;
    Error Result = ValidateArrayListAndElement(ArrayListSelf, element);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ValidateWritable(ArrayListSelf);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ValidateInsertIndex(ArrayListSelf, index);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!GenericBuffer_Insert(ArrayListSelf->_activeBuffer, element, index))
    {
        return CreateCapacityEnsureError(ArrayListSelf->_activeBuffer->_count + 1);
    }

    return Error_CreateSuccess();
}

static Error ArrayList_ListRemove(void* self, size_t index)
{
    ArrayList* ArrayListSelf = self;
    Error Result = ValidateWritable(ArrayListSelf);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ValidateExistingIndex(ArrayListSelf, index);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!GenericBuffer_RemoveAt(ArrayListSelf->_activeBuffer, index))
    {
        return CreateIndexOutOfBoundsError(index, ArrayListSelf->_activeBuffer->_count);
    }

    return Error_CreateSuccess();
}

static Error ArrayList_ListReplace(void* self, size_t index, void* element)
{
    ArrayList* ArrayListSelf = self;
    Error Result = ValidateArrayListAndElement(ArrayListSelf, element);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ValidateWritable(ArrayListSelf);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ValidateExistingIndex(ArrayListSelf, index);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!GenericBuffer_Replace(ArrayListSelf->_activeBuffer, element, index))
    {
        return CreateIndexOutOfBoundsError(index, ArrayListSelf->_activeBuffer->_count);
    }

    return Error_CreateSuccess();
}

static Error ArrayList_ListInsertRange(void* self, size_t index, void* elements, size_t elementCount)
{
    ArrayList* ArrayListSelf = self;
    Error Result = ValidateArrayListAndElements(ArrayListSelf, elements, elementCount);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ValidateWritable(ArrayListSelf);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ValidateInsertIndex(ArrayListSelf, index);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!GenericBuffer_InsertRange(ArrayListSelf->_activeBuffer, elements, elementCount, index))
    {
        return CreateCapacityEnsureError(ArrayListSelf->_activeBuffer->_count + elementCount);
    }

    return Error_CreateSuccess();
}

static Error ArrayList_ListRemoveRange(void* self, size_t index, size_t elementCount)
{
    ArrayList* ArrayListSelf = self;
    Error Result = ValidateWritable(ArrayListSelf);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ValidateExistingRange(ArrayListSelf, index, elementCount);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!GenericBuffer_RemoveRange(ArrayListSelf->_activeBuffer, index, index + elementCount))
    {
        return CreateRangeOutOfBoundsError(index, elementCount, ArrayListSelf->_activeBuffer->_count);
    }

    return Error_CreateSuccess();
}

static Error ArrayList_ListReplaceRange(void* self, size_t index, void* elements, size_t elementCount)
{
    ArrayList* ArrayListSelf = self;
    Error Result = ValidateArrayListAndElements(ArrayListSelf, elements, elementCount);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ValidateWritable(ArrayListSelf);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ValidateExistingRange(ArrayListSelf, index, elementCount);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!GenericBuffer_ReplaceRange(ArrayListSelf->_activeBuffer, elements, elementCount, index))
    {
        return CreateRangeOutOfBoundsError(index, elementCount, ArrayListSelf->_activeBuffer->_count);
    }

    return Error_CreateSuccess();
}

static Error ArrayList_ListClear(void* self)
{
    ArrayList* ArrayListSelf = self;
    Error Result = ValidateWritable(ArrayListSelf);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    if (!GenericBuffer_Clear(ArrayListSelf->_activeBuffer))
    {
        return CreateReadOnlyError();
    }
    return Error_CreateSuccess();
}

static Error ArrayList_ListGetElement(void* self, size_t index, void* outElement)
{
    ArrayList* ArrayListSelf = self;
    Error Result = Error_CreateSuccess();

    if (ArrayListSelf == NULL)
    {
        return CreateNullArgumentErrorForList(u8"self");
    }
    if (outElement == NULL)
    {
        return CreateNullArgumentErrorForList(u8"outElement");
    }

    Result = ValidateExistingIndex(ArrayListSelf, index);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!GenericBuffer_GetAt(ArrayListSelf->_activeBuffer, index, outElement))
    {
        return CreateIndexOutOfBoundsError(index, ArrayListSelf->_activeBuffer->_count);
    }

    return Error_CreateSuccess();
}

static Error ArrayList_ListGetPointerToElement(void* self, size_t index, void** outElementPointer)
{
    ArrayList* ArrayListSelf = self;
    Error Result = Error_CreateSuccess();
    void* ElementPointer = NULL;

    if (ArrayListSelf == NULL)
    {
        return CreateNullArgumentErrorForList(u8"self");
    }
    if (outElementPointer == NULL)
    {
        return CreateNullArgumentErrorForList(u8"outElementPointer");
    }

    Result = ValidateExistingIndex(ArrayListSelf, index);
    if (Result.Code != ErrorCode_Success)
    {
        *outElementPointer = NULL;
        return Result;
    }

    ElementPointer = GenericBuffer_GetPointerToElement(ArrayListSelf->_activeBuffer, index);
    if (ElementPointer == NULL)
    {
        *outElementPointer = NULL;
        return CreateIndexOutOfBoundsError(index, ArrayListSelf->_activeBuffer->_count);
    }

    *outElementPointer = ElementPointer;
    return Error_CreateSuccess();
}

static void ArrayList_ListDeconstruct(void* self)
{
    ArrayList_Deconstruct(self);
}

static Error ArrayListEnumerator_HasNext(void* self, bool* outHasNext)
{
    ArrayListEnumerator* EnumeratorSelf = self;

    if (EnumeratorSelf == NULL)
    {
        return CreateNullArgumentErrorForList(u8"self");
    }
    if (outHasNext == NULL)
    {
        return CreateNullArgumentErrorForList(u8"outHasNext");
    }

    *outHasNext = (EnumeratorSelf->_currentIndex < IList_GetElementCount(&EnumeratorSelf->_arrayList->_list));
    return Error_CreateSuccess();
}

static Error ArrayListEnumerator_NextByValue(void* self, void* outEntryValue)
{
    ArrayListEnumerator* EnumeratorSelf = self;
    bool HasNext = false;
    Error Result = ValidateEnumeratorAndOutput(EnumeratorSelf, outEntryValue);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ArrayListEnumerator_HasNext(EnumeratorSelf, &HasNext);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!HasNext)
    {
        return CreateEnumerationCompletedError();
    }

    Result = IList_GetElement(&EnumeratorSelf->_arrayList->_list, EnumeratorSelf->_currentIndex, outEntryValue);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    EnumeratorSelf->_currentIndex++;
    return Error_CreateSuccess();
}

static Error ArrayListEnumerator_NextByReference(void* self, void** outPointer)
{
    ArrayListEnumerator* EnumeratorSelf = self;
    bool HasNext = false;
    Error Result = ValidateEnumeratorPointerOutput(EnumeratorSelf, outPointer);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ArrayListEnumerator_HasNext(EnumeratorSelf, &HasNext);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!HasNext)
    {
        *outPointer = NULL;
        return CreateEnumerationCompletedError();
    }

    Result = IList_GetPointerToElement(&EnumeratorSelf->_arrayList->_list, EnumeratorSelf->_currentIndex, outPointer);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    EnumeratorSelf->_currentIndex++;
    return Error_CreateSuccess();
}

static void ArrayListEnumerator_Deconstruct(void* self)
{
    ArrayListEnumerator* EnumeratorSelf = self;

    if (EnumeratorSelf == NULL)
    {
        return;
    }

    Memory_Free(EnumeratorSelf);
}

static CollectionEnumerator* ArrayList_ListGetEnumerator(void* self)
{
    static const CollectionEnumeratorVTable EnumeratorVTableTemplate =
    {
        .Self = NULL,
        ._hasNext = ArrayListEnumerator_HasNext,
        ._nextByValue = ArrayListEnumerator_NextByValue,
        ._nextByReference = ArrayListEnumerator_NextByReference,
        ._deconstruct = ArrayListEnumerator_Deconstruct,
    };
    ArrayList* ArrayListSelf = self;
    ArrayListEnumerator* Enumerator = NULL;

    if (ArrayListSelf == NULL)
    {
        return NULL;
    }

    Enumerator = Memory_Allocate(sizeof(*Enumerator));
    Enumerator->Base._singleElementSize = IList_GetElementSize(&ArrayListSelf->_list);
    Enumerator->Base._flags = EnumeratorFlags_CanReturnByReference;
    Enumerator->Base._vtable = EnumeratorVTableTemplate;
    Enumerator->Base._vtable.Self = Enumerator;
    Enumerator->_arrayList = ArrayListSelf;
    Enumerator->_currentIndex = 0;
    return &Enumerator->Base;
}


// Public functions.
void ArrayList_Construct1(ArrayList* self, size_t elementSize)
{
    if (self == NULL)
    {
        return;
    }

    InitializeEmptyBuffer(&self->_selfContainedBuffer);
    self->_isActiveBufferOwned = true;
    self->_activeBuffer = &self->_selfContainedBuffer;
    GenericBuffer_CreateVariable(&self->_selfContainedBuffer,
        NULL,
        0,
        elementSize,
        0,
        self,
        ArrayList_RequestMoreSpace);
    InitializeInterfaces(self);
}

void ArrayList_Construct2(ArrayList* self, size_t elementSize, size_t initialCapacity)
{
    if (self == NULL)
    {
        return;
    }

    ArrayList_Construct1(self, elementSize);
    if (initialCapacity > 0)
    {
        (void)ArrayList_EnsureTotalCapacity(self, initialCapacity);
    }
}

void ArrayList_Construct3(ArrayList* self, GenericBuffer* bufferToWrap)
{
    if (self == NULL)
    {
        return;
    }

    InitializeEmptyBuffer(&self->_selfContainedBuffer);
    self->_isActiveBufferOwned = false;
    self->_activeBuffer = bufferToWrap;

    if (self->_activeBuffer == NULL)
    {
        self->_activeBuffer = &self->_selfContainedBuffer;
        self->_isActiveBufferOwned = true;
    }

    InitializeInterfaces(self);
}

Error ArrayList_EnsureTotalCapacity(ArrayList* self, size_t totalCapacity)
{
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    Result = ValidateWritable(self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (GenericBuffer_EnsureTotalCapacity(self->_activeBuffer, totalCapacity))
    {
        InitializeInterfaces(self);
        return Error_CreateSuccess();
    }

    return CreateCapacityEnsureError(totalCapacity);
}

Error ArrayList_ReserveMoreCapacity(ArrayList* self, size_t requiredExtraSize)
{
    size_t RequiredTotalCapacity = 0;
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if ((SIZE_MAX - self->_activeBuffer->_count) < requiredExtraSize)
    {
        return CreateCapacityEnsureError(SIZE_MAX);
    }

    RequiredTotalCapacity = self->_activeBuffer->_count + requiredExtraSize;
    Result = ValidateWritable(self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (GenericBuffer_ReserveMoreCapacity(self->_activeBuffer, requiredExtraSize))
    {
        InitializeInterfaces(self);
        return Error_CreateSuccess();
    }

    return CreateCapacityEnsureError(RequiredTotalCapacity);
}

void ArrayList_Deconstruct(ArrayList* self)
{
    if (self == NULL)
    {
        return;
    }

    if (self->_isActiveBufferOwned && (self->_activeBuffer != NULL) && (self->_activeBuffer->_data != NULL))
    {
        Memory_Free(self->_activeBuffer->_data);
    }

    InitializeEmptyBuffer(&self->_selfContainedBuffer);
    self->_activeBuffer = &self->_selfContainedBuffer;
    self->_isActiveBufferOwned = false;
    InitializeInterfaces(self);
}
