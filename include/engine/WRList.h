#pragma once
#include <stdint.h>
#include <stddef.h>
#include "WRError.h"
#include "WRCollection.h"
#include "WRComparator.h"
#include "WRMemory.h"


#define LIST_INDEX_INVALID (~((size_t)0))


// Types.
typedef enum IListFlagsEnum
{
    IListFlags_None = 0,
    IListFlags_IsReadOnly = (1 << 0),
} IListFlags;

typedef struct IListVTableStruct
{
    void* Self;

    size_t (*_getElementCount)(void* self);

    Error (*_insert)(void* self, size_t index, void* element);
    Error (*_remove)(void* self, size_t index);
    Error (*_replace)(void* self, size_t index, void* element);
    Error (*_insertRange)(void* self, size_t index, void* elements, size_t elementCount);
    Error (*_removeRange)(void* self, size_t index, size_t elementCount);
    Error (*_replaceRange)(void* self, size_t index, void* elements, size_t elementCount);
    
    Error (*_clear)(void* self);

    Error (*_getElement)(void* self, size_t index, void* outElement);
    Error (*_getPointerToElement)(void* self, size_t index, void** outElementPointer);

    void (*_deconstruct)(void* self);
} IListVTable;

typedef struct IListStruct
{
    size_t _elementSize;
    IListFlags _flags;
    IListVTable _vtable;
    ICollection _collection;
} IList;

typedef struct IListElementDataStruct
{
    void* _element;
    size_t _index;
} IListElementData;

typedef bool (*IListPredicate)(IList* list, IListElementData element, void* userData);

typedef ComparisonResult (*IListComparator)(IList* list, IListElementData a, IListElementData b, void* userData);

typedef void (*IListMapper)(IList* list, IListElementData sourceElement, void* destinationElement, void* userData);

typedef int64_t (*IListIntExtractor)(IList* list, IListElementData sourceElement, void* userData);

typedef double (*IListDoubleExtractor)(IList* list, IListElementData sourceElement, void* userData);



// Functions.
static inline ICollection* IList_AsCollection(IList* self)
{
    return &self->_collection;
}

static inline CollectionEnumerator* IList_GetEnumerator(IList* self)
{
    return ICollection_GetEnumerator(IList_AsCollection(self));
}

static inline size_t IList_GetElementCount(IList* self)
{
    return self->_vtable._getElementCount(self->_vtable.Self);
}

static inline size_t IList_GetElementSize(IList* self)
{
    return self->_elementSize;
}

static inline IListFlags IList_GetFlags(IList* self)
{
    return self->_flags;
}

static inline bool IList_IsReadOnly(IList* self)
{
    return (IList_GetFlags(self) & IListFlags_IsReadOnly) != 0;
}

Error IList_AddLast(IList* self, void* element);

Error IList_AddFirst(IList* self, void* element);

Error IList_Insert(IList* self, size_t index, void* element);

Error IList_RemoveFirst(IList* self);

Error IList_RemoveLast(IList* self);

Error IList_RemoveAt(IList* self, size_t index);

Error IList_Replace(IList* self, size_t index, void* element);

Error IList_AddRangeLast(IList* self, void* elements, size_t elementCount);

Error IList_AddRangeFirst(IList* self, void* elements, size_t elementCount);

Error IList_InsertRange(IList* self, size_t index, void* elements, size_t elementCount);

Error IList_RemoveRange(IList* self, size_t index, size_t elementCount);

Error IList_ReplaceRange(IList* self, size_t index, void* elements, size_t elementCount);

Error IList_Clear(IList* self);



Error IList_GetElement(IList* self, size_t index, void* outElement);

Error IList_GetPointerToElement(IList* self, size_t index, void** outElementPointer);

Error IList_GetFirst(IList* self, void* outElement);

Error IList_GetLast(IList* self, void* outElement);



Error IList_SortAscending(IList* self, IListComparator comparator, void* userData);

Error IList_SortDescending(IList* self, IListComparator comparator, void* userData);

Error IList_Filter(IList* self, IListPredicate predicate, void* userData);

Error IList_Map(IList* self, GenericBuffer* destination, IListMapper mapper, void* userData);

Error IList_SumInt(IList* self, IListIntExtractor extractor, void* userData, int64_t* outValue);

Error IList_SumDouble(IList* self, IListDoubleExtractor extractor, void* userData, double* outValue);

Error IList_MaxInt(IList* self, IListIntExtractor extractor, void* userData, int64_t* outValue);

Error IList_MaxDouble(IList* self, IListDoubleExtractor extractor, void* userData, double* outValue);

Error IList_MinInt(IList* self, IListIntExtractor extractor, void* userData, int64_t* outValue);

Error IList_MinDouble(IList* self, IListDoubleExtractor extractor, void* userData, double* outValue);

Error IList_Contains(IList* self, IListPredicate predicate, void* userData, bool* outValue);

Error IList_CountWhere(IList* self, IListPredicate predicate, void* userData, size_t* outValue);

Error IList_FirstIndexOf(IList* self, IListPredicate predicate, void* userData, size_t* outValue);

Error IList_LastIndexOf(IList* self, IListPredicate predicate, void* userData, size_t* outValue);

Error IList_Reverse(IList* self);

Error IList_CopyTo(IList* self, GenericBuffer* destination);


Error IList_Deconstruct(IList* self);
