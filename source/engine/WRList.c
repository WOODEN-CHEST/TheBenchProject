#include "WRList.h"


// Macros.
#define LIST_SORT_ASCENDING (1)
#define LIST_SORT_DESCENDING (-1)


// Types.
typedef struct ListSortContextStruct
{
    IList* List;
    IListComparator Comparator;
    void* UserData;
    int Direction;
    unsigned char* ElementBufferA;
    unsigned char* ElementBufferB;
} ListSortContext;


// Static functions.
static Error CreateNullArgumentError(const unsigned char* argumentName)
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

static Error CreateEmptyListError(const unsigned char* operationName)
{
    return Error_Construct3(ErrorCode_InvalidOperation,
        u8"Cannot %s on an empty list.",
        operationName);
}

static Error CreateDestinationElementSizeError(size_t expectedSize, size_t actualSize)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Destination element size %zu does not match the list element size %zu.",
        actualSize,
        expectedSize);
}

static Error ValidateList(IList* self)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    return Error_CreateSuccess();
}

static Error ValidateListAndElement(IList* self, void* element)
{
    Error Result = ValidateList(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (element == NULL)
    {
        return CreateNullArgumentError(u8"element");
    }

    return Error_CreateSuccess();
}

static Error ValidateListAndElements(IList* self, void* elements, size_t elementCount)
{
    Error Result = ValidateList(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((elementCount > 0) && (elements == NULL))
    {
        return CreateNullArgumentError(u8"elements");
    }

    return Error_CreateSuccess();
}

static Error ValidateListAndOutput(IList* self, void* output, const unsigned char* outputName)
{
    Error Result = ValidateList(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (output == NULL)
    {
        return CreateNullArgumentError(outputName);
    }

    return Error_CreateSuccess();
}

static Error ValidateWritableList(IList* self)
{
    Error Result = ValidateList(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (IList_IsReadOnly(self))
    {
        return CreateReadOnlyError();
    }

    return Error_CreateSuccess();
}

static Error GetElementPointerOrCopy(IList* self, size_t index, void** outPointer, void* scratchBuffer)
{
    Error Result = IList_GetPointerToElement(self, index, outPointer);

    if (Result.Code == ErrorCode_Success)
    {
        return Error_CreateSuccess();
    }
    if (scratchBuffer == NULL)
    {
        *outPointer = NULL;
        return Result;
    }

    Result = IList_GetElement(self, index, scratchBuffer);
    if (Result.Code != ErrorCode_Success)
    {
        *outPointer = NULL;
        return Result;
    }

    *outPointer = scratchBuffer;
    return Error_CreateSuccess();
}

static Error SwapElements(IList* self, size_t indexA, size_t indexB, unsigned char* bufferA, unsigned char* bufferB)
{
    Error Result = Error_CreateSuccess();

    if (indexA == indexB)
    {
        return Error_CreateSuccess();
    }

    Result = IList_GetElement(self, indexA, bufferA);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = IList_GetElement(self, indexB, bufferB);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = IList_Replace(self, indexA, bufferB);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return IList_Replace(self, indexB, bufferA);
}

static bool ShouldSwapForSort(ComparisonResult result, int direction)
{
    return (((direction == LIST_SORT_ASCENDING) && (result == ComparisonResult_AGreaterThanB))
        || ((direction == LIST_SORT_DESCENDING) && (result == ComparisonResult_ALessThanB)));
}

static Error CompareElements(ListSortContext* context,
    size_t indexA,
    void* pointerA,
    size_t indexB,
    void* pointerB,
    ComparisonResult* outResult)
{
    IListElementData ElementA = { ._element = pointerA, ._index = indexA };
    IListElementData ElementB = { ._element = pointerB, ._index = indexB };

    if (outResult == NULL)
    {
        return CreateNullArgumentError(u8"outResult");
    }

    *outResult = context->Comparator(context->List, ElementA, ElementB, context->UserData);
    return Error_CreateSuccess();
}

static Error PartitionList(ListSortContext* context,
    size_t lowIndex,
    size_t highIndex,
    size_t* outPivotIndex)
{
    size_t PartitionIndex = lowIndex;
    Error Result = Error_CreateSuccess();

    if (outPivotIndex == NULL)
    {
        return CreateNullArgumentError(u8"outPivotIndex");
    }

    Result = IList_GetElement(context->List, highIndex, context->ElementBufferB);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    for (size_t CurrentIndex = lowIndex; CurrentIndex < highIndex; CurrentIndex++)
    {
        void* CurrentPointer = NULL;
        ComparisonResult CompareResult = ComparisonResult_AEqualsB;

        Result = GetElementPointerOrCopy(context->List,
            CurrentIndex,
            &CurrentPointer,
            context->ElementBufferA);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        Result = CompareElements(context,
            CurrentIndex,
            CurrentPointer,
            highIndex,
            context->ElementBufferB,
            &CompareResult);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        if (!ShouldSwapForSort(CompareResult, context->Direction))
        {
            Result = SwapElements(context->List,
                PartitionIndex,
                CurrentIndex,
                context->ElementBufferA,
                context->ElementBufferB);
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }

            PartitionIndex++;
        }
    }

    Result = SwapElements(context->List,
        PartitionIndex,
        highIndex,
        context->ElementBufferA,
        context->ElementBufferB);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    *outPivotIndex = PartitionIndex;
    return Error_CreateSuccess();
}

static Error QuickSortList(ListSortContext* context, size_t lowIndex, size_t highIndex)
{
    size_t PivotIndex = 0;
    Error Result = Error_CreateSuccess();

    if (lowIndex >= highIndex)
    {
        return Error_CreateSuccess();
    }

    Result = PartitionList(context, lowIndex, highIndex, &PivotIndex);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    if (PivotIndex > lowIndex)
    {
        Result = QuickSortList(context, lowIndex, PivotIndex - 1);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    if (PivotIndex < highIndex)
    {
        Result = QuickSortList(context, PivotIndex + 1, highIndex);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    return Error_CreateSuccess();
}

static Error SortInternal(IList* self, IListComparator comparator, void* userData, int direction)
{
    size_t ElementSize = 0;
    size_t ElementCount = 0;
    unsigned char* ScratchBuffer = NULL;
    ListSortContext Context;
    Error Result = Error_CreateSuccess();

    Result = ValidateWritableList(self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (comparator == NULL)
    {
        return CreateNullArgumentError(u8"comparator");
    }

    ElementCount = IList_GetElementCount(self);
    if (ElementCount < 2)
    {
        return Error_CreateSuccess();
    }

    ElementSize = IList_GetElementSize(self);
    ScratchBuffer = Memory_Allocate(ElementSize * 2);
    Context = (ListSortContext)
    {
        .List = self,
        .Comparator = comparator,
        .UserData = userData,
        .Direction = direction,
        .ElementBufferA = ScratchBuffer,
        .ElementBufferB = ScratchBuffer + ElementSize,
    };
    Result = QuickSortList(&Context, 0, ElementCount - 1);
    Memory_Free(ScratchBuffer);
    return Result;
}


// Public functions.
Error IList_AddLast(IList* self, void* element)
{
    Error Result = ValidateListAndElement(self, element);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return self->_vtable._insert(self->_vtable.Self, IList_GetElementCount(self), element);
}

Error IList_AddFirst(IList* self, void* element)
{
    Error Result = ValidateListAndElement(self, element);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return self->_vtable._insert(self->_vtable.Self, 0, element);
}

Error IList_Insert(IList* self, size_t index, void* element)
{
    Error Result = ValidateListAndElement(self, element);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return self->_vtable._insert(self->_vtable.Self, index, element);
}

Error IList_RemoveFirst(IList* self)
{
    Error Result = ValidateList(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return self->_vtable._remove(self->_vtable.Self, 0);
}

Error IList_RemoveLast(IList* self)
{
    size_t ElementCount = 0;
    Error Result = ValidateList(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    ElementCount = IList_GetElementCount(self);
    if (ElementCount == 0)
    {
        return CreateEmptyListError(u8"remove the last element");
    }

    return self->_vtable._remove(self->_vtable.Self, ElementCount - 1);
}

Error IList_RemoveAt(IList* self, size_t index)
{
    Error Result = ValidateList(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return self->_vtable._remove(self->_vtable.Self, index);
}

Error IList_Replace(IList* self, size_t index, void* element)
{
    Error Result = ValidateListAndElement(self, element);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return self->_vtable._replace(self->_vtable.Self, index, element);
}

Error IList_AddRangeLast(IList* self, void* elements, size_t elementCount)
{
    Error Result = ValidateListAndElements(self, elements, elementCount);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return self->_vtable._insertRange(self->_vtable.Self, IList_GetElementCount(self), elements, elementCount);
}

Error IList_AddRangeFirst(IList* self, void* elements, size_t elementCount)
{
    Error Result = ValidateListAndElements(self, elements, elementCount);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return self->_vtable._insertRange(self->_vtable.Self, 0, elements, elementCount);
}

Error IList_InsertRange(IList* self, size_t index, void* elements, size_t elementCount)
{
    Error Result = ValidateListAndElements(self, elements, elementCount);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return self->_vtable._insertRange(self->_vtable.Self, index, elements, elementCount);
}

Error IList_RemoveRange(IList* self, size_t index, size_t elementCount)
{
    Error Result = ValidateList(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return self->_vtable._removeRange(self->_vtable.Self, index, elementCount);
}

Error IList_ReplaceRange(IList* self, size_t index, void* elements, size_t elementCount)
{
    Error Result = ValidateListAndElements(self, elements, elementCount);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return self->_vtable._replaceRange(self->_vtable.Self, index, elements, elementCount);
}

Error IList_Clear(IList* self)
{
    Error Result = ValidateList(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return self->_vtable._clear(self->_vtable.Self);
}

Error IList_GetElement(IList* self, size_t index, void* outElement)
{
    Error Result = ValidateListAndOutput(self, outElement, u8"outElement");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return self->_vtable._getElement(self->_vtable.Self, index, outElement);
}

Error IList_GetPointerToElement(IList* self, size_t index, void** outElementPointer)
{
    Error Result = ValidateListAndOutput(self, outElementPointer, u8"outElementPointer");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return self->_vtable._getPointerToElement(self->_vtable.Self, index, outElementPointer);
}

Error IList_GetFirst(IList* self, void* outElement)
{
    Error Result = ValidateListAndOutput(self, outElement, u8"outElement");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (IList_GetElementCount(self) == 0)
    {
        return CreateEmptyListError(u8"get the first element");
    }

    return IList_GetElement(self, 0, outElement);
}

Error IList_GetLast(IList* self, void* outElement)
{
    size_t ElementCount = 0;
    Error Result = ValidateListAndOutput(self, outElement, u8"outElement");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    ElementCount = IList_GetElementCount(self);
    if (ElementCount == 0)
    {
        return CreateEmptyListError(u8"get the last element");
    }

    return IList_GetElement(self, ElementCount - 1, outElement);
}

Error IList_SortAscending(IList* self, IListComparator comparator, void* userData)
{
    return SortInternal(self, comparator, userData, LIST_SORT_ASCENDING);
}

Error IList_SortDescending(IList* self, IListComparator comparator, void* userData)
{
    return SortInternal(self, comparator, userData, LIST_SORT_DESCENDING);
}

Error IList_Filter(IList* self, IListPredicate predicate, void* userData)
{
    size_t ElementCount = 0;
    size_t WriteIndex = 0;
    size_t ElementSize = 0;
    unsigned char* ScratchBuffer = NULL;
    Error Result = ValidateWritableList(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (predicate == NULL)
    {
        return CreateNullArgumentError(u8"predicate");
    }

    ElementCount = IList_GetElementCount(self);
    ElementSize = IList_GetElementSize(self);
    ScratchBuffer = Memory_Allocate(ElementSize);

    for (size_t ReadIndex = 0; ReadIndex < ElementCount; ReadIndex++)
    {
        void* ElementPointer = NULL;
        IListElementData ElementData;

        Result = GetElementPointerOrCopy(self, ReadIndex, &ElementPointer, ScratchBuffer);
        if (Result.Code != ErrorCode_Success)
        {
            Memory_Free(ScratchBuffer);
            return Result;
        }

        ElementData = (IListElementData)
        {
            ._element = ElementPointer,
            ._index = ReadIndex,
        };
        if (!predicate(self, ElementData, userData))
        {
            continue;
        }
        if (WriteIndex != ReadIndex)
        {
            Result = IList_Replace(self, WriteIndex, ElementPointer);
            if (Result.Code != ErrorCode_Success)
            {
                Memory_Free(ScratchBuffer);
                return Result;
            }
        }

        WriteIndex++;
    }

    Memory_Free(ScratchBuffer);
    if (WriteIndex == ElementCount)
    {
        return Error_CreateSuccess();
    }

    return IList_RemoveRange(self, WriteIndex, ElementCount - WriteIndex);
}

Error IList_Map(IList* self, GenericBuffer* destination, IListMapper mapper, void* userData)
{
    size_t SourceCount = 0;
    size_t DestinationStartIndex = 0;
    unsigned char* DestinationWritePointer = NULL;
    unsigned char* ScratchBuffer = NULL;
    size_t ElementSize = 0;
    Error Result = ValidateList(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (destination == NULL)
    {
        return CreateNullArgumentError(u8"destination");
    }
    if (mapper == NULL)
    {
        return CreateNullArgumentError(u8"mapper");
    }

    SourceCount = IList_GetElementCount(self);
    DestinationStartIndex = destination->_count;
    ElementSize = IList_GetElementSize(self);
    ScratchBuffer = Memory_Allocate(ElementSize);
    if (!GenericBuffer_TryPrepareForManualMutation(destination, SourceCount))
    {
        Memory_Free(ScratchBuffer);
        return Error_Construct1(ErrorCode_InvalidOperation,
            u8"Could not prepare the destination buffer for mapped elements.");
    }

    DestinationWritePointer = destination->_data + (DestinationStartIndex * destination->_elementSize);
    for (size_t Index = 0; Index < SourceCount; Index++)
    {
        void* SourcePointer = NULL;
        IListElementData SourceElement;

        Result = GetElementPointerOrCopy(self, Index, &SourcePointer, ScratchBuffer);
        if (Result.Code != ErrorCode_Success)
        {
            Memory_Free(ScratchBuffer);
            return Result;
        }

        SourceElement = (IListElementData)
        {
            ._element = SourcePointer,
            ._index = Index,
        };
        mapper(self, SourceElement, DestinationWritePointer, userData);
        DestinationWritePointer += destination->_elementSize;
    }

    Memory_Free(ScratchBuffer);
    destination->_count = DestinationStartIndex + SourceCount;
    return Error_CreateSuccess();
}

Error IList_SumInt(IList* self, IListIntExtractor extractor, void* userData, int64_t* outValue)
{
    int64_t Sum = 0;
    size_t ElementCount = 0;
    size_t ElementSize = 0;
    unsigned char* ScratchBuffer = NULL;
    Error Result = ValidateListAndOutput(self, outValue, u8"outValue");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (extractor == NULL)
    {
        return CreateNullArgumentError(u8"extractor");
    }

    ElementCount = IList_GetElementCount(self);
    ElementSize = IList_GetElementSize(self);
    ScratchBuffer = Memory_Allocate(ElementSize);

    for (size_t Index = 0; Index < ElementCount; Index++)
    {
        void* ElementPointer = NULL;
        IListElementData ElementData;

        Result = GetElementPointerOrCopy(self, Index, &ElementPointer, ScratchBuffer);
        if (Result.Code != ErrorCode_Success)
        {
            Memory_Free(ScratchBuffer);
            return Result;
        }

        ElementData = (IListElementData)
        {
            ._element = ElementPointer,
            ._index = Index,
        };
        Sum += extractor(self, ElementData, userData);
    }

    Memory_Free(ScratchBuffer);
    *outValue = Sum;
    return Error_CreateSuccess();
}

Error IList_SumDouble(IList* self, IListDoubleExtractor extractor, void* userData, double* outValue)
{
    double Sum = 0.0;
    size_t ElementCount = 0;
    size_t ElementSize = 0;
    unsigned char* ScratchBuffer = NULL;
    Error Result = ValidateListAndOutput(self, outValue, u8"outValue");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (extractor == NULL)
    {
        return CreateNullArgumentError(u8"extractor");
    }

    ElementCount = IList_GetElementCount(self);
    ElementSize = IList_GetElementSize(self);
    ScratchBuffer = Memory_Allocate(ElementSize);

    for (size_t Index = 0; Index < ElementCount; Index++)
    {
        void* ElementPointer = NULL;
        IListElementData ElementData;

        Result = GetElementPointerOrCopy(self, Index, &ElementPointer, ScratchBuffer);
        if (Result.Code != ErrorCode_Success)
        {
            Memory_Free(ScratchBuffer);
            return Result;
        }

        ElementData = (IListElementData)
        {
            ._element = ElementPointer,
            ._index = Index,
        };
        Sum += extractor(self, ElementData, userData);
    }

    Memory_Free(ScratchBuffer);
    *outValue = Sum;
    return Error_CreateSuccess();
}

Error IList_MaxInt(IList* self, IListIntExtractor extractor, void* userData, int64_t* outValue)
{
    size_t ElementCount = 0;
    size_t ElementSize = 0;
    unsigned char* ScratchBuffer = NULL;
    int64_t MaxValue = 0;
    Error Result = ValidateListAndOutput(self, outValue, u8"outValue");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (extractor == NULL)
    {
        return CreateNullArgumentError(u8"extractor");
    }

    ElementCount = IList_GetElementCount(self);
    if (ElementCount == 0)
    {
        return CreateEmptyListError(u8"get the maximum integer value");
    }

    ElementSize = IList_GetElementSize(self);
    ScratchBuffer = Memory_Allocate(ElementSize);
    for (size_t Index = 0; Index < ElementCount; Index++)
    {
        void* ElementPointer = NULL;
        IListElementData ElementData;
        int64_t Value = 0;

        Result = GetElementPointerOrCopy(self, Index, &ElementPointer, ScratchBuffer);
        if (Result.Code != ErrorCode_Success)
        {
            Memory_Free(ScratchBuffer);
            return Result;
        }

        ElementData = (IListElementData)
        {
            ._element = ElementPointer,
            ._index = Index,
        };
        Value = extractor(self, ElementData, userData);
        if ((Index == 0) || (Value > MaxValue))
        {
            MaxValue = Value;
        }
    }

    Memory_Free(ScratchBuffer);
    *outValue = MaxValue;
    return Error_CreateSuccess();
}

Error IList_MaxDouble(IList* self, IListDoubleExtractor extractor, void* userData, double* outValue)
{
    size_t ElementCount = 0;
    size_t ElementSize = 0;
    unsigned char* ScratchBuffer = NULL;
    double MaxValue = 0.0;
    Error Result = ValidateListAndOutput(self, outValue, u8"outValue");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (extractor == NULL)
    {
        return CreateNullArgumentError(u8"extractor");
    }

    ElementCount = IList_GetElementCount(self);
    if (ElementCount == 0)
    {
        return CreateEmptyListError(u8"get the maximum floating-point value");
    }

    ElementSize = IList_GetElementSize(self);
    ScratchBuffer = Memory_Allocate(ElementSize);
    for (size_t Index = 0; Index < ElementCount; Index++)
    {
        void* ElementPointer = NULL;
        IListElementData ElementData;
        double Value = 0.0;

        Result = GetElementPointerOrCopy(self, Index, &ElementPointer, ScratchBuffer);
        if (Result.Code != ErrorCode_Success)
        {
            Memory_Free(ScratchBuffer);
            return Result;
        }

        ElementData = (IListElementData)
        {
            ._element = ElementPointer,
            ._index = Index,
        };
        Value = extractor(self, ElementData, userData);
        if ((Index == 0) || (Value > MaxValue))
        {
            MaxValue = Value;
        }
    }

    Memory_Free(ScratchBuffer);
    *outValue = MaxValue;
    return Error_CreateSuccess();
}

Error IList_MinInt(IList* self, IListIntExtractor extractor, void* userData, int64_t* outValue)
{
    size_t ElementCount = 0;
    size_t ElementSize = 0;
    unsigned char* ScratchBuffer = NULL;
    int64_t MinValue = 0;
    Error Result = ValidateListAndOutput(self, outValue, u8"outValue");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (extractor == NULL)
    {
        return CreateNullArgumentError(u8"extractor");
    }

    ElementCount = IList_GetElementCount(self);
    if (ElementCount == 0)
    {
        return CreateEmptyListError(u8"get the minimum integer value");
    }

    ElementSize = IList_GetElementSize(self);
    ScratchBuffer = Memory_Allocate(ElementSize);
    for (size_t Index = 0; Index < ElementCount; Index++)
    {
        void* ElementPointer = NULL;
        IListElementData ElementData;
        int64_t Value = 0;

        Result = GetElementPointerOrCopy(self, Index, &ElementPointer, ScratchBuffer);
        if (Result.Code != ErrorCode_Success)
        {
            Memory_Free(ScratchBuffer);
            return Result;
        }

        ElementData = (IListElementData)
        {
            ._element = ElementPointer,
            ._index = Index,
        };
        Value = extractor(self, ElementData, userData);
        if ((Index == 0) || (Value < MinValue))
        {
            MinValue = Value;
        }
    }

    Memory_Free(ScratchBuffer);
    *outValue = MinValue;
    return Error_CreateSuccess();
}

Error IList_MinDouble(IList* self, IListDoubleExtractor extractor, void* userData, double* outValue)
{
    size_t ElementCount = 0;
    size_t ElementSize = 0;
    unsigned char* ScratchBuffer = NULL;
    double MinValue = 0.0;
    Error Result = ValidateListAndOutput(self, outValue, u8"outValue");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (extractor == NULL)
    {
        return CreateNullArgumentError(u8"extractor");
    }

    ElementCount = IList_GetElementCount(self);
    if (ElementCount == 0)
    {
        return CreateEmptyListError(u8"get the minimum floating-point value");
    }

    ElementSize = IList_GetElementSize(self);
    ScratchBuffer = Memory_Allocate(ElementSize);
    for (size_t Index = 0; Index < ElementCount; Index++)
    {
        void* ElementPointer = NULL;
        IListElementData ElementData;
        double Value = 0.0;

        Result = GetElementPointerOrCopy(self, Index, &ElementPointer, ScratchBuffer);
        if (Result.Code != ErrorCode_Success)
        {
            Memory_Free(ScratchBuffer);
            return Result;
        }

        ElementData = (IListElementData)
        {
            ._element = ElementPointer,
            ._index = Index,
        };
        Value = extractor(self, ElementData, userData);
        if ((Index == 0) || (Value < MinValue))
        {
            MinValue = Value;
        }
    }

    Memory_Free(ScratchBuffer);
    *outValue = MinValue;
    return Error_CreateSuccess();
}

Error IList_Contains(IList* self, IListPredicate predicate, void* userData, bool* outValue)
{
    size_t Index = 0;
    size_t ElementSize = 0;
    size_t ElementCount = 0;
    unsigned char* ScratchBuffer = NULL;
    Error Result = ValidateListAndOutput(self, outValue, u8"outValue");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (predicate == NULL)
    {
        return CreateNullArgumentError(u8"predicate");
    }

    *outValue = false;
    ElementCount = IList_GetElementCount(self);
    ElementSize = IList_GetElementSize(self);
    ScratchBuffer = Memory_Allocate(ElementSize);
    for (Index = 0; Index < ElementCount; Index++)
    {
        void* ElementPointer = NULL;
        IListElementData ElementData;

        Result = GetElementPointerOrCopy(self, Index, &ElementPointer, ScratchBuffer);
        if (Result.Code != ErrorCode_Success)
        {
            Memory_Free(ScratchBuffer);
            return Result;
        }

        ElementData = (IListElementData)
        {
            ._element = ElementPointer,
            ._index = Index,
        };
        if (predicate(self, ElementData, userData))
        {
            *outValue = true;
            break;
        }
    }

    Memory_Free(ScratchBuffer);
    return Error_CreateSuccess();
}

Error IList_CountWhere(IList* self, IListPredicate predicate, void* userData, size_t* outValue)
{
    size_t Count = 0;
    size_t ElementCount = 0;
    size_t ElementSize = 0;
    unsigned char* ScratchBuffer = NULL;
    Error Result = ValidateListAndOutput(self, outValue, u8"outValue");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (predicate == NULL)
    {
        return CreateNullArgumentError(u8"predicate");
    }

    ElementCount = IList_GetElementCount(self);
    ElementSize = IList_GetElementSize(self);
    ScratchBuffer = Memory_Allocate(ElementSize);
    for (size_t Index = 0; Index < ElementCount; Index++)
    {
        void* ElementPointer = NULL;
        IListElementData ElementData;

        Result = GetElementPointerOrCopy(self, Index, &ElementPointer, ScratchBuffer);
        if (Result.Code != ErrorCode_Success)
        {
            Memory_Free(ScratchBuffer);
            return Result;
        }

        ElementData = (IListElementData)
        {
            ._element = ElementPointer,
            ._index = Index,
        };
        if (predicate(self, ElementData, userData))
        {
            Count++;
        }
    }

    Memory_Free(ScratchBuffer);
    *outValue = Count;
    return Error_CreateSuccess();
}

Error IList_FirstIndexOf(IList* self, IListPredicate predicate, void* userData, size_t* outValue)
{
    size_t ElementCount = 0;
    size_t ElementSize = 0;
    unsigned char* ScratchBuffer = NULL;
    Error Result = ValidateListAndOutput(self, outValue, u8"outValue");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (predicate == NULL)
    {
        return CreateNullArgumentError(u8"predicate");
    }

    *outValue = LIST_INDEX_INVALID;
    ElementCount = IList_GetElementCount(self);
    ElementSize = IList_GetElementSize(self);
    ScratchBuffer = Memory_Allocate(ElementSize);
    for (size_t Index = 0; Index < ElementCount; Index++)
    {
        void* ElementPointer = NULL;
        IListElementData ElementData;

        Result = GetElementPointerOrCopy(self, Index, &ElementPointer, ScratchBuffer);
        if (Result.Code != ErrorCode_Success)
        {
            Memory_Free(ScratchBuffer);
            return Result;
        }

        ElementData = (IListElementData)
        {
            ._element = ElementPointer,
            ._index = Index,
        };
        if (predicate(self, ElementData, userData))
        {
            *outValue = Index;
            break;
        }
    }

    Memory_Free(ScratchBuffer);
    return Error_CreateSuccess();
}

Error IList_LastIndexOf(IList* self, IListPredicate predicate, void* userData, size_t* outValue)
{
    size_t ElementCount = 0;
    size_t ElementSize = 0;
    unsigned char* ScratchBuffer = NULL;
    Error Result = ValidateListAndOutput(self, outValue, u8"outValue");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (predicate == NULL)
    {
        return CreateNullArgumentError(u8"predicate");
    }

    *outValue = LIST_INDEX_INVALID;
    ElementCount = IList_GetElementCount(self);
    ElementSize = IList_GetElementSize(self);
    ScratchBuffer = Memory_Allocate(ElementSize);
    for (size_t Index = ElementCount; Index > 0; Index--)
    {
        void* ElementPointer = NULL;
        IListElementData ElementData;

        Result = GetElementPointerOrCopy(self, Index - 1, &ElementPointer, ScratchBuffer);
        if (Result.Code != ErrorCode_Success)
        {
            Memory_Free(ScratchBuffer);
            return Result;
        }

        ElementData = (IListElementData)
        {
            ._element = ElementPointer,
            ._index = Index - 1,
        };
        if (predicate(self, ElementData, userData))
        {
            *outValue = Index - 1;
            break;
        }
    }

    Memory_Free(ScratchBuffer);
    return Error_CreateSuccess();
}

Error IList_Reverse(IList* self)
{
    size_t ElementCount = 0;
    size_t ElementSize = 0;
    unsigned char* ScratchBuffer = NULL;
    Error Result = ValidateWritableList(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    ElementCount = IList_GetElementCount(self);
    if (ElementCount < 2)
    {
        return Error_CreateSuccess();
    }

    ElementSize = IList_GetElementSize(self);
    ScratchBuffer = Memory_Allocate(ElementSize * 2);
    for (size_t Index = 0; Index < (ElementCount / 2); Index++)
    {
        size_t OppositeIndex = ElementCount - Index - 1;

        Result = SwapElements(self,
            Index,
            OppositeIndex,
            ScratchBuffer,
            ScratchBuffer + ElementSize);
        if (Result.Code != ErrorCode_Success)
        {
            Memory_Free(ScratchBuffer);
            return Result;
        }
    }

    Memory_Free(ScratchBuffer);
    return Error_CreateSuccess();
}

Error IList_CopyTo(IList* self, GenericBuffer* destination)
{
    size_t ElementCount = 0;
    size_t ElementSize = 0;
    size_t DestinationStartIndex = 0;
    unsigned char* DestinationWritePointer = NULL;
    unsigned char* ScratchBuffer = NULL;
    Error Result = ValidateList(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (destination == NULL)
    {
        return CreateNullArgumentError(u8"destination");
    }
    if (destination->_elementSize != IList_GetElementSize(self))
    {
        return CreateDestinationElementSizeError(IList_GetElementSize(self), destination->_elementSize);
    }

    ElementCount = IList_GetElementCount(self);
    ElementSize = IList_GetElementSize(self);
    DestinationStartIndex = destination->_count;
    ScratchBuffer = Memory_Allocate(ElementSize);
    if (!GenericBuffer_TryPrepareForManualMutation(destination, ElementCount))
    {
        Memory_Free(ScratchBuffer);
        return Error_Construct1(ErrorCode_InvalidOperation,
            u8"Could not prepare the destination buffer for copied list elements.");
    }

    DestinationWritePointer = destination->_data + (DestinationStartIndex * destination->_elementSize);
    for (size_t Index = 0; Index < ElementCount; Index++)
    {
        void* ElementPointer = NULL;

        Result = GetElementPointerOrCopy(self, Index, &ElementPointer, ScratchBuffer);
        if (Result.Code != ErrorCode_Success)
        {
            Memory_Free(ScratchBuffer);
            return Result;
        }

        Memory_Copy(ElementPointer, DestinationWritePointer, destination->_elementSize);
        DestinationWritePointer += destination->_elementSize;
    }

    Memory_Free(ScratchBuffer);
    destination->_count = DestinationStartIndex + ElementCount;
    return Error_CreateSuccess();
}

Error IList_Deconstruct(IList* self)
{
    Error Result = ValidateList(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    (*self->_vtable._deconstruct)(self->_vtable.Self);
    return Error_CreateSuccess();
}
