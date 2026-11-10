#include "UILayouts.h"
#include <inttypes.h>
#include <stdio.h>
#include "GameJSON.h"
#include "Logger.h"
#include "NamespacedKey.h"
#include "UIPlacement.h"
#include "UIWindowTypes.h"
#include "wr/WRCollection.h"
#include "wr/WRFileStream.h"
#include "wr/WRFileSystem.h"
#include "wr/WRIO.h"
#include "wr/WRJSON.h"
#include "wr/WRMath.h"
#include "wr/WRString.h"


// Macros.
/** The most memory a document's parsed tree may take. */
#define MAX_TREE_BYTES ((size_t)(8U * 1024U * 1024U))
/** What the temporary file a save writes first is called after the file's own name. */
#define TEMPORARY_SUFFIX ((const unsigned char*)u8".tmp")
/** How far apart two items' ranges are while a dock's items are renumbered, so the two never meet. */
#define RENUMBER_OFFSET ((uint64_t)1U << 32U)
/** Room for a key a message names. */
#define NAMED_KEY_CAPACITY ((size_t)64)
/** The JSON keys. */
#define KEY_VERSION ((const unsigned char*)u8"version")
#define KEY_ACTIVE ((const unsigned char*)u8"active")
#define KEY_LAYOUTS ((const unsigned char*)u8"layouts")
#define KEY_NAME ((const unsigned char*)u8"name")
#define KEY_WINDOWS ((const unsigned char*)u8"windows")
#define KEY_DOCK ((const unsigned char*)u8"dock")
#define KEY_PLACES ((const unsigned char*)u8"places")
#define KEY_TYPE ((const unsigned char*)u8"type")
#define KEY_BOX ((const unsigned char*)u8"box")
#define KEY_MINIMIZED ((const unsigned char*)u8"minimized")
#define KEY_MAXIMIZED ((const unsigned char*)u8"maximized")
#define KEY_SPLIT ((const unsigned char*)u8"split")
#define KEY_RATIO ((const unsigned char*)u8"ratio")
#define KEY_FIRST ((const unsigned char*)u8"first")
#define KEY_SECOND ((const unsigned char*)u8"second")
#define KEY_TABS ((const unsigned char*)u8"tabs")
#define KEY_SELECTED ((const unsigned char*)u8"selected")
#define SPLIT_ACROSS ((const unsigned char*)u8"across")
#define SPLIT_DOWN ((const unsigned char*)u8"down")


// Types.
/* What reading a document found that is not wrong, for one log line each: keys it passed over, windows and places of types not registered. */
typedef struct ReadContextStruct
{
    UILayouts* Layouts;
    size_t UnknownKeyCount;
    unsigned char FirstUnknownKey[NAMED_KEY_CAPACITY];
    size_t DroppedCount;
    unsigned char FirstDroppedType[NAMESPACED_KEY_BUFFER_SIZE];
} ReadContext;

/* One window of a document as read, before the dropped ones go. */
typedef struct FileWindowStruct
{
    UILayoutWindow Window;
    uint32_t TypeIndex;
    bool IsKept;
} FileWindow;


// Static functions: helpers.
static bool IsFiniteFloat(float value)
{
    return !Math_IsNaNFloat(value) && !Math_IsInfinityFloat(value);
}

static bool IsBoxValid(Rectangle box)
{
    return IsFiniteFloat(box.x) && IsFiniteFloat(box.y) && (Math_AbsFloat(box.x) <= UI_LAYOUT_MAX_LENGTH) && (Math_AbsFloat(box.y) <= UI_LAYOUT_MAX_LENGTH)
        && IsFiniteFloat(box.width) && IsFiniteFloat(box.height) && (box.width > 0.0f) && (box.height > 0.0f) && (box.width <= UI_LAYOUT_MAX_LENGTH)
        && (box.height <= UI_LAYOUT_MAX_LENGTH);
}

static bool IsSameText(const unsigned char* a, const unsigned char* b)
{
    size_t Length = StringUTF8_GetByteLength(a);
    return (Length == StringUTF8_GetByteLength(b)) && Memory_IsEqual(a, b, Length);
}

static UILayout* GetLayoutAt(const UILayouts* self, size_t index)
{
    return GenericBuffer_GetPointerToElement((GenericBuffer*)&self->_layouts, index);
}

static void CopyText(unsigned char* destination, size_t capacity, const unsigned char* source, size_t length)
{
    size_t Count = (length < capacity) ? length : (capacity - 1U);
    Memory_Copy(source, destination, Count);
    destination[Count] = 0U;
}

static void LogWarning(const UILayouts* self, const char* format, const char* first, const char* second)
{
    if (self->_logger == NULL)
    {
        return;
    }
    Error Result = Logger_LogWarningFormatted(self->_logger, (const unsigned char*)format, first, second);
    Error_Deconstruct(&Result);
}


// Static functions: a layout's dock compared.
static bool AreNodesEqual(const UIDockTree* a, int32_t aIndex, const UIDockTree* b, int32_t bIndex, size_t depth)
{
    const UIDockNode* First = UIDockTree_GetNode(a, aIndex);
    const UIDockNode* Second = UIDockTree_GetNode(b, bIndex);
    if ((First == NULL) || (Second == NULL) || (depth > UI_DOCK_TREE_MAX_DEPTH))
    {
        return (First == NULL) && (Second == NULL);
    }
    if (First->Kind != Second->Kind)
    {
        return false;
    }
    if (First->Kind == UIDockNodeKind_Split)
    {
        return (First->Split == Second->Split) && (First->Ratio == Second->Ratio)
            && AreNodesEqual(a, First->Children[0], b, Second->Children[0], depth + 1U)
            && AreNodesEqual(a, First->Children[1], b, Second->Children[1], depth + 1U);
    }
    size_t Count = UIDockTree_GetTabCount(a, aIndex);
    if ((Count != UIDockTree_GetTabCount(b, bIndex)) || (First->Selected != Second->Selected))
    {
        return false;
    }
    for (size_t Tab = 0U; Tab < Count; Tab++)
    {
        if (UIDockTree_GetTabAt(a, aIndex, Tab) != UIDockTree_GetTabAt(b, bIndex, Tab))
        {
            return false;
        }
    }
    return true;
}


// Public functions: a layout.
void UILayout_Construct(UILayout* self)
{
    Memory_Zero(self, sizeof(*self));
    UIDockTree_Construct(&self->Tree);
}

bool UILayout_IsNameValid(const unsigned char* name)
{
    if ((name == NULL) || !StringUTF8_IsEncodingValid(name))
    {
        return false;
    }
    size_t Length = StringUTF8_GetByteLength(name);
    if ((Length == 0U) || (Length > UI_LAYOUT_MAX_NAME_BYTES))
    {
        return false;
    }
    for (size_t Index = 0U; Index < Length; Index++)
    {
        if ((name[Index] < 0x20U) || (name[Index] == 0x7FU))
        {
            return false;
        }
    }
    return true;
}

Error UILayout_SetName(UILayout* self, const unsigned char* name)
{
    if (!UILayout_IsNameValid(name))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UILayout_SetName: a name is 1 to 64 bytes of UTF-8 without control characters.");
    }
    CopyText(self->Name, sizeof(self->Name), name, StringUTF8_GetByteLength(name));
    return Error_CreateSuccess();
}

Error UILayout_AddWindow(UILayout* self, uint32_t typeKeyId, Rectangle box, bool isMinimized, bool isMaximized, size_t* outIndex)
{
    if (!IsBoxValid(box))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UILayout_AddWindow: the box must be finite, near enough, its size above 0.");
    }
    if (self->WindowCount >= UI_LAYOUT_MAX_WINDOWS)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UILayout_AddWindow: the layout holds as many windows as it can.");
    }
    self->Windows[self->WindowCount] = (UILayoutWindow){ .TypeKeyId = typeKeyId, .Box = box, .IsMinimized = isMinimized, .IsMaximized = isMaximized };
    if (outIndex != NULL)
    {
        *outIndex = self->WindowCount;
    }
    self->WindowCount++;
    return Error_CreateSuccess();
}

Error UILayout_SetPlace(UILayout* self, uint32_t typeKeyId, Rectangle box)
{
    if (!IsBoxValid(box))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UILayout_SetPlace: the box must be finite, near enough, its size above 0.");
    }
    for (size_t Index = 0U; Index < self->PlaceCount; Index++)
    {
        if (self->Places[Index].TypeKeyId == typeKeyId)
        {
            self->Places[Index].Box = box;
            return Error_CreateSuccess();
        }
    }
    if (self->PlaceCount >= UI_LAYOUT_MAX_PLACES)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UILayout_SetPlace: the layout holds as many places as it can.");
    }
    self->Places[self->PlaceCount++] = (UILayoutPlace){ .TypeKeyId = typeKeyId, .Box = box };
    return Error_CreateSuccess();
}

bool UILayout_IsWindowDocked(const UILayout* self, size_t index)
{
    return (index < self->WindowCount) && UIDockTree_FindTab(&self->Tree, (uint64_t)index + 1U, NULL, NULL);
}

bool UILayout_IsValid(const UILayout* self)
{
    if (!UILayout_IsNameValid(self->Name) || (self->WindowCount > UI_LAYOUT_MAX_WINDOWS) || (self->PlaceCount > UI_LAYOUT_MAX_PLACES)
        || !UIDockTree_IsValid(&self->Tree) || UIDockTree_HasEmptyLeaf(&self->Tree))
    {
        return false;
    }
    size_t Maximized = 0U;
    for (size_t Index = 0U; Index < self->WindowCount; Index++)
    {
        const UILayoutWindow* Window = &self->Windows[Index];
        if (!IsBoxValid(Window->Box) || (Window->IsMinimized && UILayout_IsWindowDocked(self, Index)))
        {
            return false;
        }
        Maximized += (Window->IsMaximized && !Window->IsMinimized) ? 1U : 0U; // A minimized one may keep that it was.
    }
    for (size_t Tab = 0U; Tab < UIDockTree_GetTotalTabCount(&self->Tree); Tab++)
    {
        uint64_t Item = UI_DOCK_ITEM_NONE;
        (void)UIDockTree_GetTab(&self->Tree, Tab, &Item, NULL);
        if ((Item == UI_DOCK_ITEM_NONE) || (Item > (uint64_t)self->WindowCount))
        {
            return false;
        }
    }
    for (size_t Index = 0U; Index < self->PlaceCount; Index++)
    {
        if (!IsBoxValid(self->Places[Index].Box))
        {
            return false;
        }
        for (size_t Other = Index + 1U; Other < self->PlaceCount; Other++)
        {
            if (self->Places[Other].TypeKeyId == self->Places[Index].TypeKeyId)
            {
                return false;
            }
        }
    }
    return Maximized <= 1U;
}

bool UILayout_Equals(const UILayout* a, const UILayout* b)
{
    if (!IsSameText(a->Name, b->Name) || (a->WindowCount != b->WindowCount) || (a->PlaceCount != b->PlaceCount))
    {
        return false;
    }
    for (size_t Index = 0U; Index < a->WindowCount; Index++)
    {
        const UILayoutWindow* First = &a->Windows[Index];
        const UILayoutWindow* Second = &b->Windows[Index];
        bool IsSame = (First->TypeKeyId == Second->TypeKeyId) && (First->Box.x == Second->Box.x) && (First->Box.y == Second->Box.y)
            && (First->Box.width == Second->Box.width) && (First->Box.height == Second->Box.height) && (First->IsMinimized == Second->IsMinimized)
            && (First->IsMaximized == Second->IsMaximized);
        if (!IsSame)
        {
            return false;
        }
    }
    for (size_t Index = 0U; Index < a->PlaceCount; Index++)
    {
        bool IsFound = false;
        for (size_t Other = 0U; (Other < b->PlaceCount) && !IsFound; Other++)
        {
            const UILayoutPlace* First = &a->Places[Index];
            const UILayoutPlace* Second = &b->Places[Other];
            IsFound = (First->TypeKeyId == Second->TypeKeyId) && (First->Box.x == Second->Box.x) && (First->Box.y == Second->Box.y)
                && (First->Box.width == Second->Box.width) && (First->Box.height == Second->Box.height);
        }
        if (!IsFound)
        {
            return false;
        }
    }
    return AreNodesEqual(&a->Tree, UIDockTree_GetRoot(&a->Tree), &b->Tree, UIDockTree_GetRoot(&b->Tree), 0U);
}


// Public functions: the layouts.
Error UILayouts_Construct(UILayouts* self, const unsigned char* path, UIWindowTypes* types, KeyRegistry* keys, Logger* logger)
{
    if ((self == NULL) || (path == NULL) || (types == NULL) || (keys == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UILayouts_Construct: self, path, types and keys must not be NULL.");
    }
    Memory_Zero(self, sizeof(*self));
    GenericBuffer_AllocateVariable(&self->_layouts, 2U, sizeof(UILayout));
    GenericBuffer_AllocateVariable(&self->_path, StringUTF8_GetByteLength(path) + 1U, sizeof(unsigned char));
    if (!GenericBuffer_AppendString(&self->_path, path) || !GenericBuffer_NullTerminate(&self->_path))
    {
        Memory_Free(self->_layouts._data);
        Memory_Free(self->_path._data);
        Memory_Zero(self, sizeof(*self));
        return Error_Construct2(ErrorCode_BufferTooSmall, "UILayouts_Construct: the path could not be kept.");
    }
    self->_types = types;
    self->_keys = keys;
    self->_logger = logger;
    return Error_CreateSuccess();
}

void UILayouts_Deconstruct(UILayouts* self)
{
    if (self == NULL)
    {
        return;
    }
    Memory_Free(self->_layouts._data);
    Memory_Free(self->_path._data);
    Memory_Zero(self, sizeof(*self));
}

const UILayout* UILayouts_Get(const UILayouts* self, size_t index)
{
    return (index < self->_layouts._count) ? GetLayoutAt(self, index) : NULL;
}

Error UILayouts_SetActiveIndex(UILayouts* self, size_t index)
{
    if (index >= self->_layouts._count)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UILayouts_SetActiveIndex: no layout at that index.");
    }
    self->_activeIndex = index;
    return Error_CreateSuccess();
}

bool UILayouts_FindByName(const UILayouts* self, const unsigned char* name, size_t* outIndex)
{
    for (size_t Index = 0U; (name != NULL) && (Index < self->_layouts._count); Index++)
    {
        if (IsSameText(GetLayoutAt(self, Index)->Name, name))
        {
            *outIndex = Index;
            return true;
        }
    }
    return false;
}

Error UILayouts_Add(UILayouts* self, const UILayout* layout, size_t* outIndex)
{
    size_t Taken = 0U;
    if (!UILayout_IsValid(layout) || UILayouts_FindByName(self, layout->Name, &Taken))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UILayouts_Add: the layout must be whole and its name no other layout's.");
    }
    if (self->_layouts._count >= UI_LAYOUTS_MAX_COUNT)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UILayouts_Add: there are as many layouts as there may be.");
    }
    void* Tail = NULL;
    if (!GenericBuffer_GetWritableTail(&self->_layouts, 1U, &Tail))
    {
        return Error_Construct2(ErrorCode_BufferTooSmall, "UILayouts_Add: the layouts could not grow.");
    }
    Memory_Copy(layout, Tail, sizeof(UILayout));
    (void)GenericBuffer_CommitCount(&self->_layouts, 1U);
    if (outIndex != NULL)
    {
        *outIndex = self->_layouts._count - 1U;
    }
    return Error_CreateSuccess();
}

Error UILayouts_Store(UILayouts* self, size_t index, const UILayout* layout)
{
    if (index >= self->_layouts._count)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UILayouts_Store: no layout at that index.");
    }
    UILayout* Target = GetLayoutAt(self, index);
    unsigned char Name[UI_LAYOUT_NAME_CAPACITY];
    Memory_Copy(Target->Name, Name, sizeof(Name));
    UILayout Staged = *layout;
    Memory_Copy(Name, Staged.Name, sizeof(Name));
    if (!UILayout_IsValid(&Staged))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UILayouts_Store: the arrangement is not whole.");
    }
    *Target = Staged;
    return Error_CreateSuccess();
}

Error UILayouts_Rename(UILayouts* self, size_t index, const unsigned char* name)
{
    size_t Taken = 0U;
    if ((index >= self->_layouts._count) || !UILayout_IsNameValid(name) || (UILayouts_FindByName(self, name, &Taken) && (Taken != index)))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UILayouts_Rename: no layout at that index, or a name not valid or taken.");
    }
    return UILayout_SetName(GetLayoutAt(self, index), name);
}

Error UILayouts_Remove(UILayouts* self, size_t index)
{
    if (index >= self->_layouts._count)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UILayouts_Remove: no layout at that index.");
    }
    if (!GenericBuffer_RemoveAt(&self->_layouts, index))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UILayouts_Remove: the layout could not be taken away.");
    }
    if ((self->_activeIndex > index) || ((self->_activeIndex == index) && (index > 0U)))
    {
        self->_activeIndex--;
    }
    return Error_CreateSuccess();
}

void UILayouts_MakeName(const UILayouts* self, const unsigned char* base, unsigned char* outName)
{
    size_t BaseLength = StringUTF8_GetByteLength(base);
    CopyText(outName, UI_LAYOUT_NAME_CAPACITY, base, BaseLength);
    size_t Taken = 0U;
    for (uint32_t Number = 2U; UILayouts_FindByName(self, outName, &Taken) && (Number <= (uint32_t)(UI_LAYOUTS_MAX_COUNT + 2U)); Number++)
    {
        char Suffix[16];
        int Written = snprintf(Suffix, sizeof(Suffix), " %" PRIu32, Number);
        size_t SuffixLength = (Written > 0) ? (size_t)Written : 0U;
        size_t Kept = (BaseLength + SuffixLength <= UI_LAYOUT_MAX_NAME_BYTES) ? BaseLength : (UI_LAYOUT_MAX_NAME_BYTES - SuffixLength);
        CopyText(outName, UI_LAYOUT_NAME_CAPACITY, base, Kept);
        CopyText(outName + Kept, UI_LAYOUT_NAME_CAPACITY - Kept, (const unsigned char*)Suffix, SuffixLength);
    }
}


// Public functions: saving when the changes settle.
void UILayouts_NoteChange(UILayouts* self, double timeSeconds)
{
    self->_hasChange = true;
    self->_changeTime = timeSeconds;
}

bool UILayouts_IsSaveDue(const UILayouts* self, double timeSeconds)
{
    return self->_hasChange && ((timeSeconds - self->_changeTime) >= UI_LAYOUTS_SETTLE_SECONDS);
}


// Static functions: reading a document.
/* Notes a key a compound holds that the reader does not know: passed over, the first one named in the log. */
static Error CheckKeys(ReadContext* context, JSONCompound* compound, const unsigned char* const* known, size_t knownCount)
{
    ICollection* Keys = JSONCompound_GetKeyCollection(compound);
    CollectionEnumerator* Enumerator = (Keys != NULL) ? ICollection_CreateEnumerator(Keys) : NULL;
    if (Enumerator == NULL)
    {
        return Error_Construct2(ErrorCode_BufferTooSmall, "UI layouts: could not go through an object's keys.");
    }
    bool HasNext = false;
    Error Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
    while ((Result.Code == ErrorCode_Success) && HasNext)
    {
        unsigned char* Key = NULL;
        Result = CollectionEnumerator_NextByValue(Enumerator, &Key);
        bool IsKnown = false;
        for (size_t Index = 0U; (Result.Code == ErrorCode_Success) && (Index < knownCount) && !IsKnown; Index++)
        {
            IsKnown = (Key != NULL) && IsSameText(Key, known[Index]);
        }
        if ((Result.Code == ErrorCode_Success) && !IsKnown && (Key != NULL))
        {
            if (context->UnknownKeyCount == 0U)
            {
                CopyText(context->FirstUnknownKey, sizeof(context->FirstUnknownKey), Key, StringUTF8_GetByteLength(Key));
            }
            context->UnknownKeyCount++;
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
        }
    }
    CollectionEnumerator_Destroy(Enumerator);
    return Result;
}

/* A key's value of a type, or none when the key is not there; any other type is an error naming the key. */
static Error GetTyped(JSONCompound* compound, const unsigned char* key, JSONValueType type, bool isRequired, JSONObjectValue* outValue,
    bool* outIsFound)
{
    bool IsFound = false;
    Error Result = JSONCompound_GetOptional(compound, key, outValue, &IsFound);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    *outIsFound = IsFound;
    if (!IsFound)
    {
        return isRequired ? Error_Construct4(ErrorCode_InvalidJSON, "UI layouts: \"%s\" is missing.", (const char*)key) : Error_CreateSuccess();
    }
    bool IsNumberWanted = type == JSONValueType_RealNumber;
    bool IsRightType = IsNumberWanted ? ((outValue->Type == JSONValueType_RealNumber) || (outValue->Type == JSONValueType_Integer))
        : (outValue->Type == type);
    return IsRightType ? Error_CreateSuccess() : Error_Construct4(ErrorCode_InvalidJSON, "UI layouts: \"%s\" has the wrong type.", (const char*)key);
}

static double GetNumber(const JSONObjectValue* value)
{
    return (value->Type == JSONValueType_Integer) ? (double)value->Value.Integer : value->Value.RealNumber;
}

/* A string value as a NUL-terminated copy, refused when it is longer than the room or holds a NUL. */
static bool CopyString(const JSONObjectValue* value, unsigned char* outText, size_t capacity)
{
    const GenericBuffer* Bytes = value->Value.String;
    if ((Bytes == NULL) || (Bytes->_count >= capacity))
    {
        return false;
    }
    const unsigned char* Data = Bytes->_data;
    for (size_t Index = 0U; Index < Bytes->_count; Index++)
    {
        if (Data[Index] == 0U)
        {
            return false;
        }
    }
    CopyText(outText, capacity, Data, Bytes->_count);
    return true;
}

static Error ReadBox(const JSONObjectValue* value, Rectangle* outBox)
{
    if ((value->Type != JSONValueType_Array) || (JSONArray_GetElementCount(value->Value.Array) != 4U))
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "UI layouts: a box is four numbers.");
    }
    float Numbers[4];
    for (size_t Index = 0U; Index < 4U; Index++)
    {
        JSONObjectValue Element;
        Error Result = JSONArray_Get(value->Value.Array, Index, &Element);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        if ((Element.Type != JSONValueType_Integer) && (Element.Type != JSONValueType_RealNumber))
        {
            return Error_Construct2(ErrorCode_InvalidJSON, "UI layouts: a box is four numbers.");
        }
        double Number = GetNumber(&Element);
        if (Math_IsNaNDouble(Number) || !(Math_AbsDouble(Number) <= (double)UI_LAYOUT_MAX_LENGTH))
        {
            return Error_Construct2(ErrorCode_InvalidJSON, "UI layouts: a box's number is not finite, or too large.");
        }
        Numbers[Index] = (float)Number;
    }
    *outBox = (Rectangle){ .x = Numbers[0], .y = Numbers[1], .width = Numbers[2], .height = Numbers[3] };
    return IsBoxValid(*outBox) ? Error_CreateSuccess() : Error_Construct2(ErrorCode_InvalidJSON, "UI layouts: a box's size must be above 0.");
}

/* A window type by its key's text: its key's id and its index, or false for a text no registered type has (noted as dropped). */
static Error ResolveType(ReadContext* context, JSONCompound* compound, uint32_t* outKeyId, uint32_t* outIndex, bool* outIsKnown)
{
    JSONObjectValue Value;
    bool IsFound = false;
    Error Result = GetTyped(compound, KEY_TYPE, JSONValueType_String, true, &Value, &IsFound);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    unsigned char Key[NAMESPACED_KEY_BUFFER_SIZE];
    *outIsKnown = CopyString(&Value, Key, sizeof(Key)) && UIWindowTypes_Find(context->Layouts->_types, Key, outIndex);
    if (*outIsKnown)
    {
        *outKeyId = UIWindowTypes_GetKeyId(context->Layouts->_types, *outIndex);
        return Error_CreateSuccess();
    }
    if (context->DroppedCount == 0U)
    {
        size_t Length = (Value.Value.String != NULL) ? Value.Value.String->_count : 0U;
        CopyText(context->FirstDroppedType, sizeof(context->FirstDroppedType), (Length > 0U) ? Value.Value.String->_data : (const unsigned char*)u8"",
            Length);
    }
    context->DroppedCount++;
    return Error_CreateSuccess();
}

static Error ReadWindow(ReadContext* context, const JSONObjectValue* value, FileWindow* outWindow)
{
    if (value->Type != JSONValueType_Compound)
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "UI layouts: a window is an object.");
    }
    JSONCompound* Compound = value->Value.Compound;
    static const unsigned char* const Known[] = { KEY_TYPE, KEY_BOX, KEY_MINIMIZED, KEY_MAXIMIZED };
    Error Result = CheckKeys(context, Compound, Known, sizeof(Known) / sizeof(Known[0]));
    *outWindow = (FileWindow){ .IsKept = false };
    if (Result.Code == ErrorCode_Success)
    {
        Result = ResolveType(context, Compound, &outWindow->Window.TypeKeyId, &outWindow->TypeIndex, &outWindow->IsKept);
    }
    JSONObjectValue Field;
    bool IsFound = false;
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetTyped(Compound, KEY_BOX, JSONValueType_Array, true, &Field, &IsFound);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadBox(&Field, &outWindow->Window.Box);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetTyped(Compound, KEY_MINIMIZED, JSONValueType_Boolean, false, &Field, &IsFound);
        outWindow->Window.IsMinimized = IsFound && (Result.Code == ErrorCode_Success) && Field.Value.Boolean;
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetTyped(Compound, KEY_MAXIMIZED, JSONValueType_Boolean, false, &Field, &IsFound);
        outWindow->Window.IsMaximized = IsFound && (Result.Code == ErrorCode_Success) && Field.Value.Boolean;
    }
    return Result;
}

static Error ReadNode(ReadContext* context, JSONCompound* node, UILayout* layout, int32_t parent, int32_t place, size_t depth,
    const FileWindow* windows, size_t windowCount, bool* usedTabs);

static Error ReadSplit(ReadContext* context, JSONCompound* node, UILayout* layout, int32_t parent, int32_t place, size_t depth,
    const FileWindow* windows, size_t windowCount, bool* usedTabs)
{
    static const unsigned char* const Known[] = { KEY_SPLIT, KEY_RATIO, KEY_FIRST, KEY_SECOND };
    Error Result = CheckKeys(context, node, Known, sizeof(Known) / sizeof(Known[0]));
    JSONObjectValue Field;
    bool IsFound = false;
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetTyped(node, KEY_SPLIT, JSONValueType_String, true, &Field, &IsFound);
    }
    unsigned char Direction[16];
    if ((Result.Code == ErrorCode_Success) && (!CopyString(&Field, Direction, sizeof(Direction))
        || (!IsSameText(Direction, SPLIT_ACROSS) && !IsSameText(Direction, SPLIT_DOWN))))
    {
        Result = Error_Construct2(ErrorCode_InvalidJSON, "UI layouts: a split is \"across\" or \"down\".");
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetTyped(node, KEY_RATIO, JSONValueType_RealNumber, true, &Field, &IsFound);
    }
    double Ratio = (Result.Code == ErrorCode_Success) ? GetNumber(&Field) : 0.0;
    if ((Result.Code == ErrorCode_Success) && !((Ratio >= 0.0) && (Ratio <= 1.0)))
    {
        Result = Error_Construct2(ErrorCode_InvalidJSON, "UI layouts: a split's ratio lies within [0, 1].");
    }
    int32_t Index = UI_DOCK_NODE_NONE;
    if (Result.Code == ErrorCode_Success)
    {
        UIDockSplit Split = IsSameText(Direction, SPLIT_ACROSS) ? UIDockSplit_Across : UIDockSplit_Down;
        Result = UIDockTree_BuildNode(&layout->Tree, parent, place, UIDockNodeKind_Split, Split, (float)Ratio, &Index);
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            Result = Error_Construct2(ErrorCode_InvalidJSON, "UI layouts: the dock has more areas or levels than a dock holds.");
        }
    }
    static const unsigned char* const Children[2] = { KEY_FIRST, KEY_SECOND };
    for (int32_t Child = 0; (Child < 2) && (Result.Code == ErrorCode_Success); Child++)
    {
        Result = GetTyped(node, Children[Child], JSONValueType_Compound, true, &Field, &IsFound);
        if (Result.Code == ErrorCode_Success)
        {
            Result = ReadNode(context, Field.Value.Compound, layout, Index, Child, depth + 1U, windows, windowCount, usedTabs);
        }
    }
    return Result;
}

static Error ReadLeaf(ReadContext* context, JSONCompound* node, UILayout* layout, int32_t parent, int32_t place, const FileWindow* windows,
    size_t windowCount, bool* usedTabs)
{
    static const unsigned char* const Known[] = { KEY_TABS, KEY_SELECTED };
    Error Result = CheckKeys(context, node, Known, sizeof(Known) / sizeof(Known[0]));
    JSONObjectValue Tabs;
    bool IsFound = false;
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetTyped(node, KEY_TABS, JSONValueType_Array, true, &Tabs, &IsFound);
    }
    size_t Count = (Result.Code == ErrorCode_Success) ? JSONArray_GetElementCount(Tabs.Value.Array) : 0U;
    if ((Result.Code == ErrorCode_Success) && ((Count == 0U) || (Count > UI_DOCK_TREE_MAX_TABS)))
    {
        Result = Error_Construct2(ErrorCode_InvalidJSON, "UI layouts: an area holds 1 to 128 tabs.");
    }
    int32_t Leaf = UI_DOCK_NODE_NONE;
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIDockTree_BuildNode(&layout->Tree, parent, place, UIDockNodeKind_Leaf, UIDockSplit_Across, 0.0f, &Leaf);
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            Result = Error_Construct2(ErrorCode_InvalidJSON, "UI layouts: the dock has more areas or levels than a dock holds.");
        }
    }
    uint64_t FirstKept = UI_DOCK_ITEM_NONE;
    for (size_t Tab = 0U; (Tab < Count) && (Result.Code == ErrorCode_Success); Tab++)
    {
        JSONObjectValue Element;
        Result = JSONArray_Get(Tabs.Value.Array, Tab, &Element);
        if ((Result.Code == ErrorCode_Success) && ((Element.Type != JSONValueType_Integer) || (Element.Value.Integer < 0)
            || ((uint64_t)Element.Value.Integer >= (uint64_t)windowCount) || usedTabs[(size_t)Element.Value.Integer]))
        {
            Result = Error_Construct2(ErrorCode_InvalidJSON, "UI layouts: a tab names a window by its index, once in the whole dock.");
        }
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }
        size_t Window = (size_t)Element.Value.Integer;
        usedTabs[Window] = true;
        if (windows[Window].IsKept)
        {
            Result = UIDockTree_AddTab(&layout->Tree, Leaf, (uint64_t)Window + 1U, Count);
            FirstKept = (FirstKept == UI_DOCK_ITEM_NONE) ? ((uint64_t)Window + 1U) : FirstKept;
        }
    }
    JSONObjectValue Selected;
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetTyped(node, KEY_SELECTED, JSONValueType_Integer, false, &Selected, &IsFound);
    }
    uint64_t Chosen = FirstKept;
    if ((Result.Code == ErrorCode_Success) && IsFound)
    {
        bool IsOwn = false;
        for (size_t Tab = 0U; (Tab < Count) && !IsOwn; Tab++)
        {
            JSONObjectValue Element;
            Error GetResult = JSONArray_Get(Tabs.Value.Array, Tab, &Element);
            IsOwn = (GetResult.Code == ErrorCode_Success) && (Element.Value.Integer == Selected.Value.Integer);
            Error_Deconstruct(&GetResult);
        }
        if (!IsOwn)
        {
            Result = Error_Construct2(ErrorCode_InvalidJSON, "UI layouts: an area's selected tab is one of its tabs.");
        }
        else if (windows[(size_t)Selected.Value.Integer].IsKept)
        {
            Chosen = (uint64_t)Selected.Value.Integer + 1U;
        }
    }
    if ((Result.Code == ErrorCode_Success) && (Chosen != UI_DOCK_ITEM_NONE))
    {
        Result = UIDockTree_SelectTab(&layout->Tree, Chosen);
    }
    return Result;
}

static Error ReadNode(ReadContext* context, JSONCompound* node, UILayout* layout, int32_t parent, int32_t place, size_t depth,
    const FileWindow* windows, size_t windowCount, bool* usedTabs)
{
    if (depth > UI_DOCK_TREE_MAX_DEPTH)
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "UI layouts: the dock is deeper than a dock goes.");
    }
    JSONObjectValue Probe;
    bool IsSplit = false;
    Error Result = JSONCompound_GetOptional(node, KEY_SPLIT, &Probe, &IsSplit);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return IsSplit ? ReadSplit(context, node, layout, parent, place, depth, windows, windowCount, usedTabs)
        : ReadLeaf(context, node, layout, parent, place, windows, windowCount, usedTabs);
}

/* The dock's items renumbered from the windows' places in the document to their places among the windows kept. */
static void RenumberItems(UIDockTree* tree, const FileWindow* windows, size_t windowCount)
{
    for (size_t Window = 0U; Window < windowCount; Window++)
    {
        Error Result = UIDockTree_ReplaceItem(tree, (uint64_t)Window + 1U, (uint64_t)Window + 1U + RENUMBER_OFFSET);
        Error_Deconstruct(&Result); // A window the dock does not hold.
    }
    size_t Kept = 0U;
    for (size_t Window = 0U; Window < windowCount; Window++)
    {
        if (!windows[Window].IsKept)
        {
            continue;
        }
        Error Result = UIDockTree_ReplaceItem(tree, (uint64_t)Window + 1U + RENUMBER_OFFSET, (uint64_t)Kept + 1U);
        Error_Deconstruct(&Result);
        Kept++;
    }
}

/* A second window of a type that opens once is dropped, its tab with it. */
static void DropSecondSingles(UIWindowTypes* types, UILayout* layout, FileWindow* windows, size_t windowCount)
{
    for (size_t Window = 0U; Window < windowCount; Window++)
    {
        const UIWindowType* Type = windows[Window].IsKept ? UIWindowTypes_Get(types, windows[Window].TypeIndex) : NULL;
        if ((Type == NULL) || !Type->_isSingle)
        {
            continue;
        }
        for (size_t Earlier = 0U; Earlier < Window; Earlier++)
        {
            if (windows[Earlier].IsKept && (windows[Earlier].TypeIndex == windows[Window].TypeIndex))
            {
                windows[Window].IsKept = false;
                break;
            }
        }
        if (!windows[Window].IsKept)
        {
            Error Result = UIDockTree_RemoveTab(&layout->Tree, (uint64_t)Window + 1U);
            Error_Deconstruct(&Result); // A floating one has no tab.
        }
    }
}

static Error ReadWindows(ReadContext* context, JSONCompound* compound, FileWindow* outWindows, size_t* outCount)
{
    JSONObjectValue Windows;
    bool IsFound = false;
    *outCount = 0U;
    Error Result = GetTyped(compound, KEY_WINDOWS, JSONValueType_Array, false, &Windows, &IsFound);
    if ((Result.Code != ErrorCode_Success) || !IsFound)
    {
        return Result;
    }
    size_t Count = JSONArray_GetElementCount(Windows.Value.Array);
    if (Count > UI_LAYOUT_MAX_WINDOWS)
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "UI layouts: a layout holds at most 128 windows.");
    }
    size_t Maximized = 0U;
    for (size_t Index = 0U; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        JSONObjectValue Element;
        Result = JSONArray_Get(Windows.Value.Array, Index, &Element);
        if (Result.Code == ErrorCode_Success)
        {
            Result = ReadWindow(context, &Element, &outWindows[Index]);
        }
        Maximized += ((Result.Code == ErrorCode_Success) && outWindows[Index].Window.IsMaximized && !outWindows[Index].Window.IsMinimized) ? 1U : 0U;
    }
    if ((Result.Code == ErrorCode_Success) && (Maximized > 1U))
    {
        Result = Error_Construct2(ErrorCode_InvalidJSON, "UI layouts: at most one window of a layout shows maximized.");
    }
    *outCount = Count;
    return Result;
}

static Error ReadPlaces(ReadContext* context, JSONCompound* compound, UILayout* layout)
{
    JSONObjectValue Places;
    bool IsFound = false;
    Error Result = GetTyped(compound, KEY_PLACES, JSONValueType_Array, false, &Places, &IsFound);
    if ((Result.Code != ErrorCode_Success) || !IsFound)
    {
        return Result;
    }
    size_t Count = JSONArray_GetElementCount(Places.Value.Array);
    if (Count > UI_LAYOUT_MAX_PLACES)
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "UI layouts: a layout holds at most 128 places.");
    }
    uint32_t SeenTypes[UI_LAYOUT_MAX_PLACES];
    size_t SeenCount = 0U;
    for (size_t Index = 0U; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        JSONObjectValue Element;
        Result = JSONArray_Get(Places.Value.Array, Index, &Element);
        if ((Result.Code == ErrorCode_Success) && (Element.Type != JSONValueType_Compound))
        {
            Result = Error_Construct2(ErrorCode_InvalidJSON, "UI layouts: a place is an object.");
        }
        static const unsigned char* const Known[] = { KEY_TYPE, KEY_BOX };
        if (Result.Code == ErrorCode_Success)
        {
            Result = CheckKeys(context, Element.Value.Compound, Known, sizeof(Known) / sizeof(Known[0]));
        }
        uint32_t KeyId = NAMESPACED_KEY_ID_NONE;
        uint32_t TypeIndex = 0U;
        bool IsKnown = false;
        if (Result.Code == ErrorCode_Success)
        {
            Result = ResolveType(context, Element.Value.Compound, &KeyId, &TypeIndex, &IsKnown);
        }
        JSONObjectValue Field;
        Rectangle Box = { 0 };
        if (Result.Code == ErrorCode_Success)
        {
            Result = GetTyped(Element.Value.Compound, KEY_BOX, JSONValueType_Array, true, &Field, &IsFound);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = ReadBox(&Field, &Box);
        }
        for (size_t Seen = 0U; (Result.Code == ErrorCode_Success) && IsKnown && (Seen < SeenCount); Seen++)
        {
            if (SeenTypes[Seen] == KeyId)
            {
                Result = Error_Construct2(ErrorCode_InvalidJSON, "UI layouts: a window type has one place in a layout.");
            }
        }
        if ((Result.Code == ErrorCode_Success) && IsKnown)
        {
            SeenTypes[SeenCount++] = KeyId;
            Result = UILayout_SetPlace(layout, KeyId, Box);
        }
    }
    return Result;
}

static Error ReadLayout(ReadContext* context, const JSONObjectValue* value, UILayout* outLayout)
{
    if (value->Type != JSONValueType_Compound)
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "UI layouts: a layout is an object.");
    }
    JSONCompound* Compound = value->Value.Compound;
    UILayout_Construct(outLayout);
    static const unsigned char* const Known[] = { KEY_NAME, KEY_WINDOWS, KEY_DOCK, KEY_PLACES };
    Error Result = CheckKeys(context, Compound, Known, sizeof(Known) / sizeof(Known[0]));
    JSONObjectValue Field;
    bool IsFound = false;
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetTyped(Compound, KEY_NAME, JSONValueType_String, true, &Field, &IsFound);
    }
    if ((Result.Code == ErrorCode_Success) && (!CopyString(&Field, outLayout->Name, sizeof(outLayout->Name)) || !UILayout_IsNameValid(outLayout->Name)))
    {
        Result = Error_Construct2(ErrorCode_InvalidJSON, "UI layouts: a layout's name is 1 to 64 bytes of UTF-8 without control characters.");
    }
    FileWindow Windows[UI_LAYOUT_MAX_WINDOWS];
    size_t WindowCount = 0U;
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadWindows(context, Compound, Windows, &WindowCount);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetTyped(Compound, KEY_DOCK, JSONValueType_Compound, false, &Field, &IsFound);
    }
    bool UsedTabs[UI_LAYOUT_MAX_WINDOWS] = { false };
    if ((Result.Code == ErrorCode_Success) && IsFound)
    {
        Result = ReadNode(context, Field.Value.Compound, outLayout, UI_DOCK_NODE_NONE, 0, 0U, Windows, WindowCount, UsedTabs);
    }
    for (size_t Window = 0U; (Result.Code == ErrorCode_Success) && (Window < WindowCount); Window++)
    {
        if (UsedTabs[Window] && Windows[Window].Window.IsMinimized)
        {
            Result = Error_Construct2(ErrorCode_InvalidJSON, "UI layouts: a docked window is never minimized.");
        }
    }
    if (Result.Code == ErrorCode_Success)
    {
        DropSecondSingles(context->Layouts->_types, outLayout, Windows, WindowCount);
        UIDockTree_CollapseEmpty(&outLayout->Tree);
        RenumberItems(&outLayout->Tree, Windows, WindowCount);
        for (size_t Window = 0U; Window < WindowCount; Window++)
        {
            if (Windows[Window].IsKept)
            {
                outLayout->Windows[outLayout->WindowCount++] = Windows[Window].Window;
            }
        }
        Result = ReadPlaces(context, Compound, outLayout);
    }
    if ((Result.Code == ErrorCode_Success) && !UILayout_IsValid(outLayout))
    {
        Result = Error_Construct2(ErrorCode_InvalidJSON, "UI layouts: a layout does not hold together.");
    }
    return Result;
}

/* Reads the layouts of a document's root into a staging buffer, and the active layout's name. */
static Error ReadDocument(ReadContext* context, const JSONObjectValue* root, GenericBuffer* staged, size_t* outActive)
{
    if (root->Type != JSONValueType_Compound)
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "UI layouts: the file must be a JSON object.");
    }
    JSONCompound* Root = root->Value.Compound;
    static const unsigned char* const Known[] = { KEY_VERSION, KEY_ACTIVE, KEY_LAYOUTS };
    Error Result = CheckKeys(context, Root, Known, sizeof(Known) / sizeof(Known[0]));
    JSONObjectValue Field;
    bool IsFound = false;
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetTyped(Root, KEY_VERSION, JSONValueType_Integer, true, &Field, &IsFound);
    }
    if ((Result.Code == ErrorCode_Success) && (Field.Value.Integer != UI_LAYOUTS_FORMAT_VERSION))
    {
        Result = Error_Construct2(ErrorCode_InvalidJSON, "UI layouts: the file is of another version.");
    }
    JSONObjectValue Layouts;
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetTyped(Root, KEY_LAYOUTS, JSONValueType_Array, true, &Layouts, &IsFound);
    }
    size_t Count = (Result.Code == ErrorCode_Success) ? JSONArray_GetElementCount(Layouts.Value.Array) : 0U;
    if ((Result.Code == ErrorCode_Success) && (Count > UI_LAYOUTS_MAX_COUNT))
    {
        Result = Error_Construct2(ErrorCode_InvalidJSON, "UI layouts: the file holds at most 16 layouts.");
    }
    // One layout at a time, on the heap: a layout is too large for the stack many times over.
    UILayout* Layout = (Result.Code == ErrorCode_Success) ? Memory_Allocate(sizeof(UILayout)) : NULL;
    for (size_t Index = 0U; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        JSONObjectValue Element;
        Result = JSONArray_Get(Layouts.Value.Array, Index, &Element);
        if (Result.Code == ErrorCode_Success)
        {
            Result = ReadLayout(context, &Element, Layout);
        }
        for (size_t Other = 0U; (Result.Code == ErrorCode_Success) && (Other < staged->_count); Other++)
        {
            if (IsSameText(((const UILayout*)GenericBuffer_GetPointerToElement(staged, Other))->Name, Layout->Name))
            {
                Result = Error_Construct2(ErrorCode_InvalidJSON, "UI layouts: two layouts have one name.");
            }
        }
        if ((Result.Code == ErrorCode_Success) && !GenericBuffer_AddLast(staged, Layout))
        {
            Result = Error_Construct2(ErrorCode_BufferTooSmall, "UI layouts: the layouts could not grow.");
        }
    }
    Memory_Free(Layout);
    *outActive = 0U;
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetTyped(Root, KEY_ACTIVE, JSONValueType_String, false, &Field, &IsFound);
    }
    unsigned char Active[UI_LAYOUT_NAME_CAPACITY];
    for (size_t Index = 0U; (Result.Code == ErrorCode_Success) && IsFound && CopyString(&Field, Active, sizeof(Active)) && (Index < staged->_count); Index++)
    {
        if (IsSameText(((const UILayout*)GenericBuffer_GetPointerToElement(staged, Index))->Name, Active))
        {
            *outActive = Index;
            break;
        }
    }
    return Result;
}


// Static functions: writing a document.
static Error SetString(JSONObjectPool* pool, JSONCompound* compound, const unsigned char* key, const unsigned char* text)
{
    JSONObjectValue Value;
    Error Result = GameJSON_CreateStringValue(pool, text, &Value);
    return (Result.Code == ErrorCode_Success) ? JSONCompound_Set(compound, key, &Value) : Result;
}

static Error SetBox(JSONObjectPool* pool, JSONCompound* compound, Rectangle box)
{
    JSONArray* Array = NULL;
    Error Result = JSONObjectPool_BorrowArray(pool, &Array);
    float Numbers[4] = { box.x, box.y, box.width, box.height };
    for (size_t Index = 0U; (Index < 4U) && (Result.Code == ErrorCode_Success); Index++)
    {
        JSONObjectValue Number = JSONObjectValue_CreateRealNumber((double)Numbers[Index]);
        Result = JSONArray_Add(Array, &Number);
    }
    JSONObjectValue Value = JSONObjectValue_CreateArray(Array);
    return (Result.Code == ErrorCode_Success) ? JSONCompound_Set(compound, KEY_BOX, &Value) : Result;
}

static Error SetFlag(JSONCompound* compound, const unsigned char* key, bool isSet)
{
    JSONObjectValue Value = JSONObjectValue_CreateBoolean(true);
    return isSet ? JSONCompound_Set(compound, key, &Value) : Error_CreateSuccess();
}

static Error SetType(const UILayouts* self, JSONObjectPool* pool, JSONCompound* compound, uint32_t keyId)
{
    const unsigned char* Text = KeyRegistry_GetText(self->_keys, keyId);
    return SetString(pool, compound, KEY_TYPE, (Text != NULL) ? Text : (const unsigned char*)u8"");
}

static Error WriteNode(const UILayout* layout, int32_t index, size_t depth, JSONObjectPool* pool, JSONCompound** outNode)
{
    const UIDockNode* Node = UIDockTree_GetNode(&layout->Tree, index);
    if ((Node == NULL) || (depth > UI_DOCK_TREE_MAX_DEPTH))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UI layouts: a dock that does not hold together cannot be written.");
    }
    Error Result = JSONObjectPool_BorrowCompound(pool, outNode);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (Node->Kind == UIDockNodeKind_Split)
    {
        Result = SetString(pool, *outNode, KEY_SPLIT, (Node->Split == UIDockSplit_Across) ? SPLIT_ACROSS : SPLIT_DOWN);
        JSONObjectValue Ratio = JSONObjectValue_CreateRealNumber((double)Node->Ratio);
        if (Result.Code == ErrorCode_Success)
        {
            Result = JSONCompound_Set(*outNode, KEY_RATIO, &Ratio);
        }
        static const unsigned char* const Children[2] = { KEY_FIRST, KEY_SECOND };
        for (int32_t Child = 0; (Child < 2) && (Result.Code == ErrorCode_Success); Child++)
        {
            JSONCompound* ChildNode = NULL;
            Result = WriteNode(layout, Node->Children[Child], depth + 1U, pool, &ChildNode);
            JSONObjectValue Value = JSONObjectValue_CreateCompound(ChildNode);
            if (Result.Code == ErrorCode_Success)
            {
                Result = JSONCompound_Set(*outNode, Children[Child], &Value);
            }
        }
        return Result;
    }
    JSONArray* Tabs = NULL;
    Result = JSONObjectPool_BorrowArray(pool, &Tabs);
    size_t Count = UIDockTree_GetTabCount(&layout->Tree, index);
    for (size_t Tab = 0U; (Tab < Count) && (Result.Code == ErrorCode_Success); Tab++)
    {
        JSONObjectValue Value = JSONObjectValue_CreateInteger((int64_t)UIDockTree_GetTabAt(&layout->Tree, index, Tab) - 1);
        Result = JSONArray_Add(Tabs, &Value);
    }
    JSONObjectValue TabsValue = JSONObjectValue_CreateArray(Tabs);
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONCompound_Set(*outNode, KEY_TABS, &TabsValue);
    }
    JSONObjectValue Selected = JSONObjectValue_CreateInteger((int64_t)UIDockTree_GetSelected(&layout->Tree, index) - 1);
    return (Result.Code == ErrorCode_Success) ? JSONCompound_Set(*outNode, KEY_SELECTED, &Selected) : Result;
}

static Error WriteLayout(const UILayouts* self, const UILayout* layout, JSONObjectPool* pool, JSONCompound** outCompound)
{
    Error Result = JSONObjectPool_BorrowCompound(pool, outCompound);
    if (Result.Code == ErrorCode_Success)
    {
        Result = SetString(pool, *outCompound, KEY_NAME, layout->Name);
    }
    JSONArray* Windows = NULL;
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONObjectPool_BorrowArray(pool, &Windows);
    }
    for (size_t Index = 0U; (Index < layout->WindowCount) && (Result.Code == ErrorCode_Success); Index++)
    {
        const UILayoutWindow* Window = &layout->Windows[Index];
        JSONCompound* Entry = NULL;
        Result = JSONObjectPool_BorrowCompound(pool, &Entry);
        if (Result.Code == ErrorCode_Success)
        {
            Result = SetType(self, pool, Entry, Window->TypeKeyId);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = SetBox(pool, Entry, Window->Box);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = SetFlag(Entry, KEY_MINIMIZED, Window->IsMinimized);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = SetFlag(Entry, KEY_MAXIMIZED, Window->IsMaximized);
        }
        JSONObjectValue Value = JSONObjectValue_CreateCompound(Entry);
        if (Result.Code == ErrorCode_Success)
        {
            Result = JSONArray_Add(Windows, &Value);
        }
    }
    JSONObjectValue WindowsValue = JSONObjectValue_CreateArray(Windows);
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONCompound_Set(*outCompound, KEY_WINDOWS, &WindowsValue);
    }
    if ((Result.Code == ErrorCode_Success) && !UIDockTree_IsEmpty(&layout->Tree))
    {
        JSONCompound* Dock = NULL;
        Result = WriteNode(layout, UIDockTree_GetRoot(&layout->Tree), 0U, pool, &Dock);
        JSONObjectValue DockValue = JSONObjectValue_CreateCompound(Dock);
        if (Result.Code == ErrorCode_Success)
        {
            Result = JSONCompound_Set(*outCompound, KEY_DOCK, &DockValue);
        }
    }
    if ((Result.Code != ErrorCode_Success) || (layout->PlaceCount == 0U))
    {
        return Result;
    }
    JSONArray* Places = NULL;
    Result = JSONObjectPool_BorrowArray(pool, &Places);
    for (size_t Index = 0U; (Index < layout->PlaceCount) && (Result.Code == ErrorCode_Success); Index++)
    {
        JSONCompound* Entry = NULL;
        Result = JSONObjectPool_BorrowCompound(pool, &Entry);
        if (Result.Code == ErrorCode_Success)
        {
            Result = SetType(self, pool, Entry, layout->Places[Index].TypeKeyId);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = SetBox(pool, Entry, layout->Places[Index].Box);
        }
        JSONObjectValue Value = JSONObjectValue_CreateCompound(Entry);
        if (Result.Code == ErrorCode_Success)
        {
            Result = JSONArray_Add(Places, &Value);
        }
    }
    JSONObjectValue PlacesValue = JSONObjectValue_CreateArray(Places);
    return (Result.Code == ErrorCode_Success) ? JSONCompound_Set(*outCompound, KEY_PLACES, &PlacesValue) : Result;
}

static Error ReleasePool(JSONObjectPool* pool, Error result)
{
    if (pool == NULL)
    {
        return result;
    }
    Error PoolResult = JSONObjectPool_Deconstruct(pool);
    if ((result.Code == ErrorCode_Success) && (PoolResult.Code != ErrorCode_Success))
    {
        return PoolResult;
    }
    Error_Deconstruct(&PoolResult);
    return result;
}


// Static functions: the file.
static bool HasByteOrderMark(const unsigned char* bytes, size_t count)
{
    return (count >= 3U) && (bytes[0] == 0xEFU) && (bytes[1] == 0xBBU) && (bytes[2] == 0xBFU);
}

/* Reads the whole file, refusing one longer than UI_LAYOUTS_MAX_FILE_BYTES before reading it. */
static Error ReadFile(const unsigned char* path, GenericBuffer* outBytes)
{
    FileStream Stream;
    Error Result = FileSystem_OpenFileStream(path, FileOpenMode_ReadBinary, &Stream);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    size_t Size = 0U;
    Result = IOStream_GetStreamSizeTotal(FileStream_AsIOStream(&Stream), &Size);
    if ((Result.Code == ErrorCode_Success) && (Size > UI_LAYOUTS_MAX_FILE_BYTES))
    {
        Result = Error_Construct2(ErrorCode_InvalidJSON, "UI layouts: the file is too long.");
    }
    if ((Result.Code == ErrorCode_Success) && !GenericBuffer_TryPrepareForManualMutation(outBytes, Size))
    {
        Result = Error_Construct2(ErrorCode_BufferTooSmall, "UI layouts: could not hold the file.");
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = IOStream_ReadAll(FileStream_AsIOStream(&Stream), outBytes);
    }
    Error CloseResult = FileStream_Deconstruct(&Stream);
    if (Result.Code == ErrorCode_Success)
    {
        return CloseResult;
    }
    Error_Deconstruct(&CloseResult);
    return Result;
}

static Error AppendPath(GenericBuffer* outPath, const unsigned char* path, const unsigned char* suffix)
{
    if (!GenericBuffer_AppendString(outPath, path) || !GenericBuffer_AppendString(outPath, suffix) || !GenericBuffer_NullTerminate(outPath))
    {
        return Error_Construct2(ErrorCode_BufferTooSmall, "UI layouts: could not build a path next to the file.");
    }
    return Error_CreateSuccess();
}

/* A bad file goes aside as <path>.bad (an older one replaced), and there are no layouts now. */
static Error PutAside(UILayouts* self, const Error* problem)
{
    const unsigned char* Path = self->_path._data;
    GenericBuffer BadPath;
    GenericBuffer_AllocateVariable(&BadPath, self->_path._count + 8U, sizeof(unsigned char));
    Error Result = AppendPath(&BadPath, Path, UI_LAYOUTS_BAD_SUFFIX);
    if (Result.Code == ErrorCode_Success)
    {
        Result = FileSystem_ReplaceEntry(Path, BadPath._data);
    }
    GenericBuffer_Clear(&self->_layouts);
    self->_activeIndex = 0U;
    const char* Problem = ((problem->Message != NULL) ? (const char*)problem->Message : "no reason given");
    if (Result.Code == ErrorCode_Success)
    {
        LogWarning(self, "UI layouts: %s is not a layouts file the game reads (%s): it is kept aside with .bad after its name, and the default layout shows.",
            (const char*)Path, Problem);
    }
    else
    {
        LogWarning(self, "UI layouts: %s is not a layouts file the game reads (%s), and could not be kept aside: the default layout shows.",
            (const char*)Path, Problem);
    }
    Memory_Free(BadPath._data);
    return Result;
}

static void LogReadNotes(const UILayouts* self, const ReadContext* context)
{
    if (self->_logger == NULL)
    {
        return;
    }
    if (context->UnknownKeyCount > 0U)
    {
        Error Result = Logger_LogWarningFormatted(self->_logger, (const unsigned char*)u8"UI layouts: %" PRIu64 " keys the game does not know "
            "were passed over (the first: \"%s\").", (uint64_t)context->UnknownKeyCount, (const char*)context->FirstUnknownKey);
        Error_Deconstruct(&Result);
    }
    if (context->DroppedCount > 0U)
    {
        Error Result = Logger_LogWarningFormatted(self->_logger, (const unsigned char*)u8"UI layouts: %" PRIu64 " windows and places of window "
            "types that are not registered were dropped (the first: \"%s\").", (uint64_t)context->DroppedCount, (const char*)context->FirstDroppedType);
        Error_Deconstruct(&Result);
    }
}

static Error ReadJSONNoted(UILayouts* self, const unsigned char* bytes, size_t byteCount, ReadContext* context)
{
    if ((bytes == NULL) && (byteCount > 0U))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UILayouts_ReadJSON: the bytes must not be NULL.");
    }
    if (byteCount > UI_LAYOUTS_MAX_FILE_BYTES)
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "UI layouts: the file is too long.");
    }
    size_t Skipped = HasByteOrderMark(bytes, byteCount) ? 3U : 0U;
    JSONObjectPool* Pool = NULL;
    Error Result = JSONObjectPool_Create(&Pool);
    // A read-only view: the parser never writes through it, but GenericBuffer's storage pointer is not const.
    GenericBuffer Source;
    GenericBuffer_CreateReadOnly(&Source, (void*)(bytes + Skipped), byteCount - Skipped, sizeof(unsigned char), byteCount - Skipped);
    JSONObjectValue Root = { .Type = JSONValueType_None };
    if (Result.Code == ErrorCode_Success)
    {
        JSONReadLimits Limits = { .MaxTreeByteCount = MAX_TREE_BYTES };
        Result = JSON_DeserializeWithLimits(Pool, &Source, &Limits, &Root);
    }
    GenericBuffer Staged;
    GenericBuffer_AllocateVariable(&Staged, 2U, sizeof(UILayout));
    size_t Active = 0U;
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadDocument(context, &Root, &Staged, &Active);
    }
    if (Result.Code == ErrorCode_Success)
    {
        GenericBuffer Old = self->_layouts;
        self->_layouts = Staged;
        Staged = Old;
        self->_activeIndex = Active;
    }
    Memory_Free(Staged._data);
    return ReleasePool(Pool, Result);
}


// Public functions: the file.
Error UILayouts_ReadJSON(UILayouts* self, const unsigned char* bytes, size_t byteCount)
{
    ReadContext Context = { .Layouts = self };
    Error Result = ReadJSONNoted(self, bytes, byteCount, &Context);
    if (Result.Code == ErrorCode_Success)
    {
        LogReadNotes(self, &Context);
    }
    return Result;
}

Error UILayouts_WriteJSON(const UILayouts* self, GenericBuffer* outBytes)
{
    JSONObjectPool* Pool = NULL;
    Error Result = JSONObjectPool_Create(&Pool);
    JSONCompound* Root = NULL;
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONObjectPool_BorrowCompound(Pool, &Root);
    }
    JSONObjectValue Version = JSONObjectValue_CreateInteger(UI_LAYOUTS_FORMAT_VERSION);
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONCompound_Set(Root, KEY_VERSION, &Version);
    }
    const UILayout* Active = UILayouts_Get(self, UILayouts_GetActiveIndex(self));
    if ((Result.Code == ErrorCode_Success) && (Active != NULL))
    {
        Result = SetString(Pool, Root, KEY_ACTIVE, Active->Name);
    }
    JSONArray* Layouts = NULL;
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONObjectPool_BorrowArray(Pool, &Layouts);
    }
    for (size_t Index = 0U; (Index < self->_layouts._count) && (Result.Code == ErrorCode_Success); Index++)
    {
        JSONCompound* Layout = NULL;
        Result = WriteLayout(self, GetLayoutAt(self, Index), Pool, &Layout);
        JSONObjectValue Value = JSONObjectValue_CreateCompound(Layout);
        if (Result.Code == ErrorCode_Success)
        {
            Result = JSONArray_Add(Layouts, &Value);
        }
    }
    JSONObjectValue LayoutsValue = JSONObjectValue_CreateArray(Layouts);
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONCompound_Set(Root, KEY_LAYOUTS, &LayoutsValue);
    }
    if (Result.Code == ErrorCode_Success)
    {
        JSONObjectValue RootValue = JSONObjectValue_CreateCompound(Root);
        Result = JSON_Serialize(&RootValue, outBytes, (JSONSerializeFlags)(JSONSerializeFlags_PrettyPrinted | JSONSerializeFlags_InlineScalarArrays));
    }
    if ((Result.Code == ErrorCode_Success) && !GenericBuffer_AppendByte(outBytes, (unsigned char)'\n'))
    {
        Result = Error_Construct2(ErrorCode_BufferTooSmall, "UI layouts: could not hold the file.");
    }
    return ReleasePool(Pool, Result);
}

Error UILayouts_Load(UILayouts* self, UILayoutsLoadResult* outResult)
{
    if (outResult != NULL)
    {
        *outResult = UILayoutsLoadResult_Missing;
    }
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    self->_isSaveBlocked = false;
    Error Result = ReadFile(self->_path._data, &Bytes);
    if ((Result.Code == ErrorCode_FileNotFound) || (Result.Code == ErrorCode_DirectoryNotFound))
    {
        Error_Deconstruct(&Result);
        Memory_Free(Bytes._data);
        return Error_CreateSuccess();
    }
    if ((Result.Code != ErrorCode_Success) && (Result.Code != ErrorCode_InvalidJSON))
    {
        // There but unreadable: the player's file stays as it is, and nothing is saved over it.
        LogWarning(self, "UI layouts: %s could not be read (%s): it is left as it is and not saved over, and the default layout shows.",
            (const char*)self->_path._data, (Result.Message != NULL) ? (const char*)Result.Message : "no reason given");
        Error_Deconstruct(&Result);
        Memory_Free(Bytes._data);
        GenericBuffer_Clear(&self->_layouts);
        self->_activeIndex = 0U;
        self->_isSaveBlocked = true;
        if (outResult != NULL)
        {
            *outResult = UILayoutsLoadResult_Unreadable;
        }
        return Error_CreateSuccess();
    }
    ReadContext Context = { .Layouts = self };
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadJSONNoted(self, Bytes._data, Bytes._count, &Context);
    }
    Memory_Free(Bytes._data);
    if (Result.Code == ErrorCode_Success)
    {
        LogReadNotes(self, &Context);
        if (outResult != NULL)
        {
            *outResult = UILayoutsLoadResult_Loaded;
        }
        return Error_CreateSuccess();
    }
    if (outResult != NULL)
    {
        *outResult = UILayoutsLoadResult_Bad;
    }
    Error AsideResult = PutAside(self, &Result);
    Error_Deconstruct(&Result);
    return AsideResult;
}

Error UILayouts_Save(UILayouts* self)
{
    self->_hasChange = false;
    if (self->_isSaveBlocked)
    {
        Error Blocked = Error_Construct2(ErrorCode_InvalidOperation, "UILayouts_Save: the file could not be read when it was loaded, so it is not saved over.");
        if (!self->_isSaveFailureLogged)
        {
            self->_isSaveFailureLogged = true;
            LogWarning(self, "UI layouts: %s is not saved (%s).", (const char*)self->_path._data,
                "it could not be read when it was loaded, and is left as it is");
        }
        return Blocked;
    }
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 1024U, sizeof(unsigned char));
    GenericBuffer Temporary;
    GenericBuffer_AllocateVariable(&Temporary, self->_path._count + 8U, sizeof(unsigned char));
    Error Result = UILayouts_WriteJSON(self, &Bytes);
    if (Result.Code == ErrorCode_Success)
    {
        Result = AppendPath(&Temporary, self->_path._data, TEMPORARY_SUFFIX);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = FileSystem_WriteAllBytes(Temporary._data, Bytes._data, Bytes._count);
        if (Result.Code == ErrorCode_Success)
        {
            Result = FileSystem_ReplaceEntry(Temporary._data, self->_path._data);
        }
        if (Result.Code != ErrorCode_Success)
        {
            // Best effort: the file itself is unchanged either way.
            Error DeleteResult = FileSystem_DeleteEntry(Temporary._data);
            Error_Deconstruct(&DeleteResult);
        }
    }
    if ((Result.Code != ErrorCode_Success) && !self->_isSaveFailureLogged)
    {
        self->_isSaveFailureLogged = true;
        LogWarning(self, "UI layouts: could not save %s (%s); they are kept while the game runs, and saved again at the next change.",
            (const char*)self->_path._data, (Result.Message != NULL) ? (const char*)Result.Message : "no reason given");
    }
    else if (Result.Code == ErrorCode_Success)
    {
        self->_isSaveFailureLogged = false;
    }
    Memory_Free(Temporary._data);
    Memory_Free(Bytes._data);
    return Result;
}
